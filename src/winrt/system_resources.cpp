#include "shim/system_services.h"
#include "shim/winrt_string_compat.h"

#include "system_services_internal.h"

#include "shim/app_model.h"

#include <winstring.h>

#include "shim/log.h"
#include "shim/winrt_vtable.h"

namespace shim::winrt {
namespace {

using system_detail::kBoundsError;

#define SHIM_IINSPECTABLE                                 \
  reinterpret_cast<Method>(&QueryInterface),              \
      reinterpret_cast<Method>(&AddRef),                  \
      reinterpret_cast<Method>(&Release),                 \
      reinterpret_cast<Method>(&GetIids),                 \
      reinterpret_cast<Method>(&GetRuntimeClassName),     \
      reinterpret_cast<Method>(&GetTrustLevel)

Object* ResourceContextAlt4Singleton() noexcept;
Object* ResourceMapCollectionSingleton() noexcept;

// --- ResourceManager ------------------------------------------------------

// El mapa de recursos que devuelve ResourceManager.MainResourceMap.
// raw kind 63 @ 0x62993994.
HRESULT SHIM_COM GetResourceMapCollection(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(ResourceMapCollectionSingleton(), out);
}

HRESULT SHIM_COM ResourceMapNull(Object* self, void** out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  return S_OK;
}

HRESULT SHIM_COM ResourceMapContext(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(ResourceContextAlt4Singleton(), out);
}

HRESULT SHIM_COM ResourceMapNoOpGetter(Object* self, void* out) noexcept {
  (void)self;
  (void)out;
  return S_OK;
}

Method g_resource_map_vtable[] = {
    reinterpret_cast<Method>(&ResourceQueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&GetResourceMapCollection),
    reinterpret_cast<Method>(&ResourceMapNull),
    reinterpret_cast<Method>(&ResourceMapContext),
    reinterpret_cast<Method>(&ResourceMapNoOpGetter),
    reinterpret_cast<Method>(&ResourceMapNoOpGetter),
};
static_assert(CountOf(g_resource_map_vtable) == 11,
              "ResourceMap kind 63 must have 11 slots");
Object g_resource_map = {g_resource_map_vtable, 1, kResourceMap};

HRESULT SHIM_COM GetMainResourceMap(Object* self, void** out) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(&g_resource_map, out);
  if (SUCCEEDED(result)) {
    log::Write("Win7 ResourceManager.Current returned local manager");
  }
  return result;
}

HRESULT SHIM_COM ResourceExists(Object* self, HSTRING name,
                                unsigned char* out) noexcept {
  (void)self;
  (void)name;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

// kind 62 @ 0x62993974. It belongs to the ResourceQueryInterface family.
Method g_resource_manager_vtable[] = {
    reinterpret_cast<Method>(&ResourceQueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&GetMainResourceMap),
    reinterpret_cast<Method>(&ResourceExists),
};
static_assert(CountOf(g_resource_manager_vtable) == 8,
              "ResourceManager kind 62 must have 8 slots");
Object g_resource_manager = {g_resource_manager_vtable, 1, kResourceManager};


// --- ResourceContext ------------------------------------------------------
//
// Esta familia es especialmente sensible a la ABI: el DLL original tiene
// cinco proyecciones distintas (kinds 54..58) y NO una sola vtable compartida.
// Cada longitud y cada aridad de abajo sale de 0x62993844..0x629938F8.

constexpr GUID kIidUnknown = {
    0x00000000, 0x0000, 0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr GUID kIidInspectable = {
    0xAF86E2E0, 0xB12D, 0x4C6A,
    {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}};
constexpr GUID kIidAgileObject = {
    0x94EA2B94, 0xE9CC, 0x49E0,
    {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
constexpr GUID kIidActivationFactory = {
    0x00000035, 0x0000, 0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr GUID kIidResourceAlt4 = {
    0x2FA22F4B, 0x707E, 0x4B27,
    {0xAD, 0x0D, 0xD0, 0xD8, 0xCD, 0x46, 0x8F, 0xD2}};
constexpr GUID kIidResourceAlt1 = {
    0x98BE9D6C, 0x6338, 0x4B31,
    {0x99, 0xDF, 0xB2, 0xB4, 0x42, 0xF1, 0x71, 0x49}};
constexpr GUID kIidResourceAlt2 = {
    0x41F752EF, 0x12AF, 0x41B9,
    {0xAB, 0x36, 0xB1, 0xEB, 0x4B, 0x51, 0x24, 0x60}};
constexpr GUID kIidResourceAlt3 = {
    0x20CF492C, 0xAF0F, 0x450B,
    {0x9D, 0xA6, 0x10, 0x6D, 0xD0, 0xC2, 0x9A, 0x39}};
constexpr GUID kIidResourceMap59 = {
    0x1E036276, 0x2F60, 0x55F6,
    {0xB7, 0xF3, 0xF8, 0x60, 0x79, 0xE6, 0x90, 0x0B}};
constexpr GUID kIidResourceMap60 = {
    0xF6D1F700, 0x49C2, 0x52AE,
    {0x81, 0x54, 0x82, 0x6F, 0x99, 0x08, 0x77, 0x3C}};
constexpr GUID kIidResourceMap61 = {
    0xAC7F26F2, 0xFEB7, 0x5B2A,
    {0x8A, 0xC4, 0x34, 0x5B, 0xC6, 0x2C, 0xAE, 0xDE}};
constexpr GUID kIidResourceManager62 = {
    0x1CC0FDFC, 0x69EE, 0x4E43,
    {0x99, 0x01, 0x47, 0xF1, 0x26, 0x87, 0xBA, 0xF7}};
constexpr GUID kIidResourceMap63 = {
    0xF744D97B, 0x9988, 0x44FB,
    {0xAB, 0xD6, 0x53, 0x78, 0x84, 0x4C, 0xFA, 0x8B}};
constexpr GUID kIidResourceCollection64 = {
    0x72284824, 0xDB8C, 0x42F8,
    {0xB0, 0x8C, 0x53, 0xFF, 0x35, 0x7D, 0xAD, 0x82}};

Object* ResourceContextAlt4Singleton() noexcept;
Object* ResourceQualifierMapSingleton() noexcept;
Object* ResourceQualifierMapAlt2Singleton() noexcept;

HRESULT SHIM_COM ReturnResourceContextAlt4(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(ResourceContextAlt4Singleton(), out);
}

HRESULT SHIM_COM ReturnResourceContextAlt4WithArg(Object* self, void* arg,
                                                   void** out) noexcept {
  (void)self;
  (void)arg;
  return ReturnSingleton(ResourceContextAlt4Singleton(), out);
}

HRESULT SHIM_COM ResourceNoOp1(Object* self) noexcept {
  (void)self;
  return S_OK;
}
HRESULT SHIM_COM ResourceNoOp2(Object* self, void* arg) noexcept {
  (void)self;
  (void)arg;
  return S_OK;
}
HRESULT SHIM_COM ResourceNoOp3(Object* self, void* a, void* b) noexcept {
  (void)self;
  (void)a;
  (void)b;
  return S_OK;
}
HRESULT SHIM_COM ResourceNoOp4(Object* self, void* a, void* b,
                               void* c) noexcept {
  (void)self;
  (void)a;
  (void)b;
  (void)c;
  return S_OK;
}

HRESULT SHIM_COM GetQualifierValues(Object* self, void** out) noexcept {
  (void)self;
  const HRESULT hr = ReturnSingleton(ResourceQualifierMapSingleton(), out);
  if (SUCCEEDED(hr)) {
    log::Write("Win7 ResourceContext.QualifierValues returned local map");
  }
  return hr;
}

HRESULT SHIM_COM GetResourceLanguages(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(LanguageVectorSingleton(), out);
}

HRESULT SHIM_COM ResourceMapEmptyPair(Object* self, void* key,
                                      unsigned long long* pair) noexcept {
  (void)self;
  (void)key;
  if (pair == nullptr) {
    return E_POINTER;
  }
  *pair = 0;
  return S_OK;
}

// kind 54, raw vtable 0x62993844: one getter, ret 8.
Method g_resource_context_vtable[] = {
    reinterpret_cast<Method>(&ResourceQueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&ReturnResourceContextAlt4),
};
static_assert(CountOf(g_resource_context_vtable) == 7,
              "resource kind 54 vtable size mismatch");

// kind 55, 0x62993860: same result but one additional input argument.
Method g_resource_context_alt1_vtable[] = {
    reinterpret_cast<Method>(&ResourceQueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&ReturnResourceContextAlt4WithArg),
};
static_assert(CountOf(g_resource_context_alt1_vtable) == 7,
              "resource kind 55 vtable size mismatch");

// kind 56, 0x6299387C: ret sizes are 8, C, 4, 8, 8.
Method g_resource_context_alt2_vtable[] = {
    reinterpret_cast<Method>(&ResourceQueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&ReturnResourceContextAlt4),
    reinterpret_cast<Method>(&ResourceNoOp3),
    reinterpret_cast<Method>(&ResourceNoOp1),
    reinterpret_cast<Method>(&ResourceNoOp2),
    reinterpret_cast<Method>(&ReturnResourceContextAlt4),
};
static_assert(CountOf(g_resource_context_alt2_vtable) == 11,
              "resource kind 56 vtable size mismatch");

// kind 57, 0x629938A8: single four-argument no-op after IInspectable.
Method g_resource_context_alt3_vtable[] = {
    reinterpret_cast<Method>(&ResourceQueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&ResourceNoOp4),
};
static_assert(CountOf(g_resource_context_alt3_vtable) == 7,
              "resource kind 57 vtable size mismatch");

// kind 58, 0x629938C4. Keep this historical symbol name because the structural
// verifier keys on it; semantically this is the fifth ResourceContext view.
Method g_resource_qualifier_vtable[] = {
    reinterpret_cast<Method>(&ResourceQueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&GetQualifierValues),
    reinterpret_cast<Method>(&ResourceNoOp1),
    reinterpret_cast<Method>(&ResourceNoOp2),
    reinterpret_cast<Method>(&ResourceNoOp2),
    reinterpret_cast<Method>(&ReturnResourceContextAlt4),
    reinterpret_cast<Method>(&GetResourceLanguages),
    reinterpret_cast<Method>(&ResourceNoOp2),
};
static_assert(CountOf(g_resource_qualifier_vtable) == 13,
              "resource kind 58 must match original 13-slot vtable");

// kind 59, 0x629938F8: local QualifierValues map. Both concrete methods use
// three stdcall arguments total (ret 0x0C in the original).
Method g_resource_qualifier_map_vtable[] = {
    reinterpret_cast<Method>(&ResourceQueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&ResourceMapEmptyPair),
    reinterpret_cast<Method>(&ResourceNoOp3),
};
static_assert(CountOf(g_resource_qualifier_map_vtable) == 8,
              "resource kind 59 vtable size mismatch");

HRESULT SHIM_COM ResourceQualifierGetValue(Object* self, HSTRING key,
                                            HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  const wchar_t* raw = shim::winrt_string::GetRawBuffer(key, nullptr);
  if (raw == nullptr) {
    return kBoundsError;
  }
  if (::lstrcmpW(raw, L"Language") == 0) {
    return shim::winrt_string::Create(L"en-US", 5, out);
  }
  if (::lstrcmpW(raw, L"Scale") == 0) {
    return shim::winrt_string::Create(L"100", 3, out);
  }
  return kBoundsError;
}

HRESULT SHIM_COM ResourceQualifierSize(Object* self,
                                        unsigned int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 2;
  return S_OK;
}

HRESULT SHIM_COM ResourceQualifierHasKey(Object* self, HSTRING key,
                                          unsigned char* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  const wchar_t* raw = shim::winrt_string::GetRawBuffer(key, nullptr);
  *out = (raw != nullptr &&
          (::lstrcmpW(raw, L"Language") == 0 ||
           ::lstrcmpW(raw, L"Scale") == 0)) ? 1 : 0;
  return S_OK;
}

HRESULT SHIM_COM ReturnResourceQualifierMapAlt2(Object* self,
                                                 void** out) noexcept {
  (void)self;
  return ReturnSingleton(ResourceQualifierMapAlt2Singleton(), out);
}

HRESULT SHIM_COM ResourceQualifierFalse4(Object* self, void* a, void* b,
                                          unsigned char* out) noexcept {
  (void)self;
  (void)a;
  (void)b;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

HRESULT SHIM_COM ResourcePairZero(Object* self, unsigned int* first,
                                   unsigned int* second) noexcept {
  (void)self;
  if (first != nullptr) {
    *first = 0;
  }
  if (second != nullptr) {
    *second = 0;
  }
  return S_OK;
}

// kinds 60/61 are the other two projections of the qualifier map family.
Method g_resource_qualifier_map_alt1_vtable[] = {
    reinterpret_cast<Method>(&ResourceQueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&ResourceQualifierGetValue),
    reinterpret_cast<Method>(&ResourceQualifierSize),
    reinterpret_cast<Method>(&ResourceQualifierHasKey),
    reinterpret_cast<Method>(&ReturnResourceQualifierMapAlt2),
    reinterpret_cast<Method>(&ResourceQualifierFalse4),
    reinterpret_cast<Method>(&ResourceNoOp2),
    reinterpret_cast<Method>(&ResourceNoOp1),
};
static_assert(CountOf(g_resource_qualifier_map_alt1_vtable) == 13,
              "resource kind 60 vtable size mismatch");

Method g_resource_qualifier_map_alt2_vtable[] = {
    reinterpret_cast<Method>(&ResourceQueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&ResourceQualifierGetValue),
    reinterpret_cast<Method>(&ResourceQualifierSize),
    reinterpret_cast<Method>(&ResourceQualifierHasKey),
    reinterpret_cast<Method>(&ResourcePairZero),
};
static_assert(CountOf(g_resource_qualifier_map_alt2_vtable) == 10,
              "resource kind 61 vtable size mismatch");

HRESULT SHIM_COM ResourceCollectionSize(Object* self,
                                         unsigned int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}
HRESULT SHIM_COM ResourceCollectionGetAt(Object* self, unsigned int index,
                                          void** out) noexcept {
  (void)self;
  (void)index;
  if (out != nullptr) {
    *out = nullptr;
  }
  return kBoundsError;
}
HRESULT SHIM_COM ResourceCollectionGetMany(Object* self, unsigned int start,
                                            unsigned int capacity,
                                            void** out) noexcept {
  (void)self;
  (void)start;
  (void)capacity;
  if (out != nullptr) {
    *out = nullptr;
  }
  return kBoundsError;
}
HRESULT SHIM_COM ResourceCollectionSelf(Object* self, void* arg,
                                         void** out) noexcept {
  (void)self;
  (void)arg;
  return ReturnSingleton(ResourceMapCollectionSingleton(), out);
}

Method g_resource_map_collection_vtable[] = {
    reinterpret_cast<Method>(&ResourceQueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&ResourceCollectionSize),
    reinterpret_cast<Method>(&ResourceCollectionGetAt),
    reinterpret_cast<Method>(&ResourceCollectionGetMany),
    reinterpret_cast<Method>(&ResourceCollectionSelf),
};
static_assert(CountOf(g_resource_map_collection_vtable) == 10,
              "resource kind 64 vtable size mismatch");

Object g_resource_context = {g_resource_context_vtable, 1, kResourceContext};
Object g_resource_context_alt1 = {g_resource_context_alt1_vtable, 1,
                                  kResourceContextAlt1};
Object g_resource_context_alt2 = {g_resource_context_alt2_vtable, 1,
                                  kResourceContextAlt2};
Object g_resource_context_alt3 = {g_resource_context_alt3_vtable, 1,
                                  kResourceContextAlt3};
Object g_resource_context_alt4 = {g_resource_qualifier_vtable, 1,
                                  kResourceContextAlt4};
Object g_resource_qualifier_map = {g_resource_qualifier_map_vtable, 1,
                                   kResourceQualifierMap};
Object g_resource_qualifier_map_alt1 = {g_resource_qualifier_map_alt1_vtable, 1,
                                        kResourceQualifierMapAlt1};
Object g_resource_qualifier_map_alt2 = {g_resource_qualifier_map_alt2_vtable, 1,
                                        kResourceQualifierMapAlt2};
Object g_resource_map_collection = {g_resource_map_collection_vtable, 1,
                                    kResourceMapCollection};

Object* ResourceContextAlt4Singleton() noexcept {
  return &g_resource_context_alt4;
}
Object* ResourceQualifierMapSingleton() noexcept {
  return &g_resource_qualifier_map;
}
Object* ResourceQualifierMapAlt2Singleton() noexcept {
  return &g_resource_qualifier_map_alt2;
}
Object* ResourceMapCollectionSingleton() noexcept {
  return &g_resource_map_collection;
}

bool SameGuid(const GUID* a, const GUID& b) noexcept {
  return a != nullptr && memcmp(a, &b, sizeof(GUID)) == 0;
}

#undef SHIM_IINSPECTABLE

}  // namespace

HRESULT SHIM_COM ResourceQueryInterface(Object* self, const GUID* iid,
                                        void** out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  if (self == nullptr || iid == nullptr) {
    return E_NOINTERFACE;
  }

  Object* sibling = nullptr;
  const bool base_iid = SameGuid(iid, kIidUnknown) ||
                        SameGuid(iid, kIidInspectable) ||
                        SameGuid(iid, kIidAgileObject);
  switch (self->kind) {
    case kResourceContext:
    case kResourceContextAlt1:
    case kResourceContextAlt2:
    case kResourceContextAlt3:
      if (SameGuid(iid, kIidResourceAlt1)) {
        sibling = &g_resource_context_alt1;
      } else if (SameGuid(iid, kIidResourceAlt2)) {
        sibling = &g_resource_context_alt2;
      } else if (SameGuid(iid, kIidResourceAlt3)) {
        sibling = &g_resource_context_alt3;
      } else if (SameGuid(iid, kIidActivationFactory) || base_iid) {
        sibling = &g_resource_context;
      }
      break;

    case kResourceContextAlt4:
      if (SameGuid(iid, kIidResourceAlt4) || base_iid) {
        sibling = &g_resource_context_alt4;
      }
      break;

    case kResourceQualifierMap:
    case kResourceQualifierMapAlt1:
    case kResourceQualifierMapAlt2:
      if (SameGuid(iid, kIidResourceMap60)) {
        sibling = &g_resource_qualifier_map_alt1;
      } else if (SameGuid(iid, kIidResourceMap61)) {
        sibling = &g_resource_qualifier_map_alt2;
      } else if (SameGuid(iid, kIidResourceMap59) || base_iid) {
        sibling = &g_resource_qualifier_map;
      }
      break;

    case kResourceManager:
      if (SameGuid(iid, kIidResourceManager62) || base_iid) {
        sibling = &g_resource_manager;
      }
      break;

    case kResourceMap:
      if (SameGuid(iid, kIidResourceMap63) || base_iid) {
        sibling = &g_resource_map;
      }
      break;

    case kResourceMapCollection:
      if (SameGuid(iid, kIidResourceCollection64) || base_iid) {
        sibling = &g_resource_map_collection;
      }
      break;

    default:
      return E_NOINTERFACE;
  }

  if (sibling == nullptr) {
    return E_NOINTERFACE;
  }
  return ReturnSingleton(sibling, out);
}

namespace system_detail {

Object* ResourceManagerSingleton() noexcept { return &g_resource_manager; }

Object* ResolveResourceContext(const GUID* iid) noexcept {
  Object* resolved = nullptr;
  if (SUCCEEDED(ResourceQueryInterface(&g_resource_context, iid,
                                       reinterpret_cast<void**>(&resolved))) &&
      resolved != nullptr) {
    return resolved;
  }
  return &g_resource_context_alt2;
}

}  // namespace system_detail
}  // namespace shim::winrt
