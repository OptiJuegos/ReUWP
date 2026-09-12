#include "shim/winrt_vtable.h"

#include <cstring>

#include "shim/app_model.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/system_services.h"

namespace shim::winrt {
namespace {

constexpr HRESULT kBoundsError = static_cast<HRESULT>(0x8000000BL);

constexpr GUID kConnectionProfile2Iid = {
    0xE2045145, 0x4C9F, 0x400C,
    {0x91, 0x50, 0x7E, 0xC7, 0xD6, 0xE2, 0x88, 0x8A}};
constexpr GUID kAsyncInfoIid = {
    0x00000036, 0x0000, 0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

bool SameGuid(const IID* iid, const GUID& expected) noexcept {
  return iid != nullptr && std::memcmp(iid, &expected, sizeof(GUID)) == 0;
}

// Shared empty-IIterable projection used by original kinds 11, 15 and 22.
// Raw objects: kind 16 @ 0x62997654, kind 17 @ 0x62997660.
Object* EmptyIteratorProjection() noexcept;

HRESULT SHIM_COM EmptyIterableFirst(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(EmptyIteratorProjection(), out);
}

HRESULT SHIM_COM EmptyIteratorCurrent(Object* self, void** out) noexcept {
  (void)self;
  if (out != nullptr) {
    *out = nullptr;
  }
  return kBoundsError;
}

HRESULT SHIM_COM EmptyIteratorBool(Object* self,
                                   unsigned char* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

HRESULT SHIM_COM EmptyIteratorGetMany(Object* self, unsigned int capacity,
                                      void** items,
                                      unsigned int* written) noexcept {
  (void)self;
  (void)capacity;
  (void)items;
  if (written == nullptr) {
    return E_POINTER;
  }
  *written = 0;
  return S_OK;
}

Method g_empty_iterable_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&EmptyIterableFirst),
};
static_assert(CountOf(g_empty_iterable_vtable) == 7,
              "empty IIterable projection must have 7 slots");

Method g_empty_iterator_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&EmptyIteratorCurrent),
    reinterpret_cast<Method>(&EmptyIteratorBool),
    reinterpret_cast<Method>(&EmptyIteratorBool),
    reinterpret_cast<Method>(&EmptyIteratorGetMany),
};
static_assert(CountOf(g_empty_iterator_vtable) == 10,
              "empty IIterator projection must have 10 slots");

Object g_empty_iterable_projection = {g_empty_iterable_vtable, 1, 16};
Object g_empty_iterator_projection = {g_empty_iterator_vtable, 1, 17};
Object* EmptyIteratorProjection() noexcept { return &g_empty_iterator_projection; }

Object* EmptyIterableProjection() noexcept {
  return &g_empty_iterable_projection;
}

// 1.1.5-only IAsyncInfo projection, raw object kind 82 @ 0x62997BF4 and
// vtable 0x62993C78. Its five concrete methods end in ret 8/8/8/4/4.
HRESULT SHIM_COM AsyncInfoOne(Object* self, unsigned int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 1;
  return S_OK;
}

HRESULT SHIM_COM AsyncInfoZero(Object* self, unsigned int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

HRESULT SHIM_COM AsyncInfoNoOp(Object* self) noexcept {
  (void)self;
  return S_OK;
}

Method g_async_info_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&AsyncInfoOne),
    reinterpret_cast<Method>(&AsyncInfoOne),
    reinterpret_cast<Method>(&AsyncInfoZero),
    reinterpret_cast<Method>(&AsyncInfoNoOp),
    reinterpret_cast<Method>(&AsyncInfoNoOp),
};
static_assert(CountOf(g_async_info_vtable) == 11,
              "IAsyncInfo projection must have 11 slots");
Object g_async_info_projection = {g_async_info_vtable, 1, 82};

Object* AsyncInfoProjection() noexcept { return &g_async_info_projection; }

bool IsAsyncOperationKind(int kind) noexcept {
  switch (kind) {
    case 28:
    case 29:
    case 30:
    case 53:
    case 70:
    case 77:
    case 81:
      return true;
    default:
      return false;
  }
}

}  // namespace

HRESULT SHIM_COM QueryInterface(Object* self, const IID* iid,
                                void** out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  if (self == nullptr) {
    return E_POINTER;
  }

  if (self->kind == kCoreApplication &&
      (SameGuid(iid, kCoreApplicationIid) ||
       SameGuid(iid, kCoreApplicationExitIid))) {
    return ReturnSingleton(CoreApplicationForInterface(iid), out);
  }

  // sub_6294A660 first adds the full IAsyncInfo view on the modern (1.1.5)
  // branch for seven async-operation kinds. Returning the operation itself here
  // leaves the caller indexing the shorter operation vtable as IAsyncInfo.
  if (game::HasCapability(game::VersionCapability::kAsyncInfoProjection) &&
      IsAsyncOperationKind(self->kind) && SameGuid(iid, kAsyncInfoIid)) {
    log::Write("IAsyncOperation QI returned full IAsyncInfo projection");
    return ReturnSingleton(AsyncInfoProjection(), out);
  }

  // ConnectionProfile has a real second interface. The IID is copied directly
  // from raw data at 0x62988D4C.
  if (self->kind == 12 && SameGuid(iid, kConnectionProfile2Iid)) {
    log::Write("ConnectionProfile QI returned IConnectionProfile2");
    return ReturnSingleton(ConnectionProfile2Projection(), out);
  }

  // These projections are selected by kind in the original even when the IID
  // is not inspected. They are what make IMap/IVectorView enumerable.
  switch (self->kind) {
    case 11:
    case 15:
    case 22:
      return ReturnSingleton(EmptyIterableProjection(), out);
    case 18:
      return ReturnSingleton(HostNamesIterableProjection(), out);
    default:
      break;
  }

  // All remaining objects use the original fallback: same object, refcount +1.
  *out = self;
  AddRef(self);
  return S_OK;
}

HRESULT __cdecl NotImplementedNoCleanup() noexcept {
  // Exact reconstruction of raw 0x6294E040: E_NOTIMPL + plain RET.
  // Keep this distinct from literal NULL vtable entries.
  log::Write("WinRT: original no-cleanup E_NOTIMPL slot invoked");
  return E_NOTIMPL;
}

}  // namespace shim::winrt
