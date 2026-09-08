#include "shim/store.h"
#include "shim/winrt_string_compat.h"

#include <winstring.h>

#include "shim/log.h"
// El GetResults compartido sirve tambien a las operaciones de los dialogos de
// fichero, que viven en el modulo de almacenamiento. Es una dependencia del
// original, no del diseno: en el binario es literalmente la misma funcion.
#include "shim/storage.h"
#include "shim/winrt_vtable.h"

namespace shim::store {

using winrt::AddRef;
using winrt::Method;
using winrt::ReturnSingleton;

namespace {

// Etiquetas de los singletons de este modulo, leidas de la tabla que construye
// Win32Bootstrap a partir de 0x62997570. Ya no son conjeturas.
constexpr int kListingInformationKind = 10;
constexpr int kEmptyCollectionKind = 11;
constexpr int kEmptyVectorKind = 22;

// La operacion que devuelve LoadListingInformationAsync NO lleva el `kind` del
// ListingInformation: lleva el 28, que el GetResults compartido no reconoce y
// por tanto cae al caso por defecto, que es justamente devolver el
// ListingInformation. El rodeo es del original.
constexpr int kListingOperationKind = 28;

// Store objects are touched very early in the 0.15.10 startup path.  Their
// vtables must be the exact lengths from the original DLL; shortening one does
// not fail at the missing method.  The caller indexes past the array and can
// land on the next global vtable (often QueryInterface), so the crash appears
// later with perfectly valid `this` and nonsensical IID/out arguments.

constexpr HRESULT kBounds = static_cast<HRESULT>(0x8000000B);
inline constexpr wchar_t kListingMinecraft[] = L"Minecraft";
inline constexpr UINT32 kListingMinecraftLength = 9;

// 0x6294AC90 / 0x6294ACB0 -- ILicenseInformation boolean properties.
HRESULT SHIM_COM GetLicenseIsActive(Object* self, BYTE* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 1;
  return S_OK;
}

HRESULT SHIM_COM GetLicenseIsTrial(Object* self, BYTE* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

// 0x6294ACD0 -- ILicenseInformation::add_LicenseChanged.  The shim never
// raises the event; the original only zeroes the 64-bit registration token.
HRESULT SHIM_COM AddLicenseChanged(Object* self, void* handler,
                                   LONGLONG* token) noexcept {
  (void)self;
  (void)handler;
  if (token == nullptr) {
    return E_POINTER;
  }
  *token = 0;
  return S_OK;
}

// 0x6294AD90 -- the four ListingInformation string properties used by the
// original all return the same nine-character HSTRING: "Minecraft".
HRESULT SHIM_COM GetListingMinecraftString(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(kListingMinecraft, kListingMinecraftLength, out);
}

// 0x6294ADF0 -- final ListingInformation property: null + S_OK.
HRESULT SHIM_COM GetNullObject(Object* self, void** out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  return S_OK;
}

// 0x6294AE10..0x6294AE70 -- empty IMapView used by ProductLicenses and
// ProductListings.  Keep the exact ABI of all four slots.
HRESULT SHIM_COM EmptyMapLookup(Object* self, HSTRING key, void** out) noexcept {
  (void)self;
  (void)key;
  if (out != nullptr) {
    *out = nullptr;
  }
  return kBounds;
}

HRESULT SHIM_COM EmptyMapGetSize(Object* self, UINT32* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

HRESULT SHIM_COM EmptyMapHasKey(Object* self, HSTRING key, BYTE* found) noexcept {
  (void)self;
  (void)key;
  if (found == nullptr) {
    return E_POINTER;
  }
  *found = 0;
  return S_OK;
}

HRESULT SHIM_COM EmptyMapSplit(Object* self, void** first,
                               void** second) noexcept {
  (void)self;
  if (first != nullptr) {
    *first = nullptr;
  }
  if (second != nullptr) {
    *second = nullptr;
  }
  return S_OK;
}

// 0x6294B690..0x6294B700 -- distinct empty IVectorView returned by
// GetUnfulfilledConsumablesAsync.  It is NOT the map above: IndexOf and
// GetMany have different arities, which matters under x86 __stdcall.
HRESULT SHIM_COM EmptyVectorGetAt(Object* self, UINT32 index,
                                  void** out) noexcept {
  (void)self;
  (void)index;
  if (out != nullptr) {
    *out = nullptr;
  }
  return kBounds;
}

HRESULT SHIM_COM EmptyVectorGetSize(Object* self, UINT32* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

HRESULT SHIM_COM EmptyVectorIndexOf(Object* self, Object* item,
                                    UINT32* index, BYTE* found) noexcept {
  (void)self;
  (void)item;
  if (index == nullptr || found == nullptr) {
    return E_POINTER;
  }
  *index = 0;
  *found = 0;
  return S_OK;
}

HRESULT SHIM_COM EmptyVectorGetMany(Object* self, UINT32 start,
                                    UINT32 capacity, void** items,
                                    UINT32* written) noexcept {
  (void)self;
  (void)start;
  (void)capacity;
  (void)items;
  if (written == nullptr) {
    return E_POINTER;
  }
  *written = 0;
  return S_OK;
}

// 0x629931CC -- IMapView<HSTRING, ...>, 10 entries exactly.
Method g_empty_collection_vtable[] = {
    reinterpret_cast<Method>(&winrt::QueryInterface),
    reinterpret_cast<Method>(&winrt::AddRef),
    reinterpret_cast<Method>(&winrt::Release),
    reinterpret_cast<Method>(&winrt::GetIids),
    reinterpret_cast<Method>(&winrt::GetRuntimeClassName),
    reinterpret_cast<Method>(&winrt::GetTrustLevel),
    reinterpret_cast<Method>(&EmptyMapLookup),
    reinterpret_cast<Method>(&EmptyMapGetSize),
    reinterpret_cast<Method>(&EmptyMapHasKey),
    reinterpret_cast<Method>(&EmptyMapSplit),
};
static_assert(CountOf(g_empty_collection_vtable) == 10,
              "empty Store map vtable must match 0x629931CC");

// 0x62993338 -- IVectorView<...>, also 10 entries but a different ABI.
Method g_empty_vector_vtable[] = {
    reinterpret_cast<Method>(&winrt::QueryInterface),
    reinterpret_cast<Method>(&winrt::AddRef),
    reinterpret_cast<Method>(&winrt::Release),
    reinterpret_cast<Method>(&winrt::GetIids),
    reinterpret_cast<Method>(&winrt::GetRuntimeClassName),
    reinterpret_cast<Method>(&winrt::GetTrustLevel),
    reinterpret_cast<Method>(&EmptyVectorGetAt),
    reinterpret_cast<Method>(&EmptyVectorGetSize),
    reinterpret_cast<Method>(&EmptyVectorIndexOf),
    reinterpret_cast<Method>(&EmptyVectorGetMany),
};
static_assert(CountOf(g_empty_vector_vtable) == 10,
              "empty Store vector vtable must match 0x62993338");

// 0x6299316C -- ILicenseInformation, 12 entries exactly.
Method g_license_vtable[] = {
    reinterpret_cast<Method>(&winrt::QueryInterface),       //  0
    reinterpret_cast<Method>(&winrt::AddRef),               //  1
    reinterpret_cast<Method>(&winrt::Release),              //  2
    reinterpret_cast<Method>(&winrt::GetIids),              //  3
    reinterpret_cast<Method>(&winrt::GetRuntimeClassName),  //  4
    reinterpret_cast<Method>(&winrt::GetTrustLevel),        //  5
    reinterpret_cast<Method>(&GetEmptyCollection),          //  6 AD00
    reinterpret_cast<Method>(&GetLicenseIsActive),          //  7 AC90
    reinterpret_cast<Method>(&GetLicenseIsTrial),           //  8 ACB0
    nullptr,                                                 //  9 original NULL
    reinterpret_cast<Method>(&AddLicenseChanged),           // 10 ACD0
    nullptr,                                                 // 11 original NULL
};
static_assert(CountOf(g_license_vtable) == 12,
              "LicenseInformation vtable must match 0x6299316C");

// 0x6299319C -- IListingInformation, 12 entries exactly.
Method g_listing_vtable[] = {
    reinterpret_cast<Method>(&winrt::QueryInterface),
    reinterpret_cast<Method>(&winrt::AddRef),
    reinterpret_cast<Method>(&winrt::Release),
    reinterpret_cast<Method>(&winrt::GetIids),
    reinterpret_cast<Method>(&winrt::GetRuntimeClassName),
    reinterpret_cast<Method>(&winrt::GetTrustLevel),
    reinterpret_cast<Method>(&GetListingMinecraftString),  //  6 AD90
    reinterpret_cast<Method>(&GetListingMinecraftString),  //  7 AD90
    reinterpret_cast<Method>(&GetEmptyCollection),         //  8 AD00
    reinterpret_cast<Method>(&GetListingMinecraftString),  //  9 AD90
    reinterpret_cast<Method>(&GetListingMinecraftString),  // 10 AD90
    reinterpret_cast<Method>(&GetNullObject),              // 11 ADF0
};
static_assert(CountOf(g_listing_vtable) == 12,
              "ListingInformation vtable must match 0x6299319C");

// Vtable de las operaciones asincronas: 9 entradas, verificada contra la del
// binario en 0x62993484.
Method g_async_vtable[] = {
    reinterpret_cast<Method>(&winrt::QueryInterface),
    reinterpret_cast<Method>(&winrt::AddRef),
    reinterpret_cast<Method>(&winrt::Release),
    reinterpret_cast<Method>(&winrt::GetIids),
    reinterpret_cast<Method>(&winrt::GetRuntimeClassName),
    reinterpret_cast<Method>(&winrt::GetTrustLevel),
    reinterpret_cast<Method>(&SetAsyncCompletedHandler),
    reinterpret_cast<Method>(&GetAsyncCompletedHandler),
    reinterpret_cast<Method>(&GetAsyncResults),
};

Object g_license = {g_license_vtable, 1, kLicenseInformation};
Object g_listing = {g_listing_vtable, 1, kListingInformationKind};
Object g_empty_collection = {g_empty_collection_vtable, 1, kEmptyCollectionKind};
Object g_empty_vector = {g_empty_vector_vtable, 1, kEmptyVectorKind};

// Operaciones asincronas. Las tres comparten vtable y se distinguen SOLO por su
// `kind`, que es lo que lee GetAsyncResults para saber que devolver.
Object g_boolean_operation = {g_async_vtable, 1, kBooleanOperation};
Object g_speech_operation = {g_async_vtable, 1, kSpeechSynthesisOperation};
Object g_null_operation = {g_async_vtable, 1, kNullResultOperation};
Object g_receipt_operation = {g_async_vtable, 1, kReceiptOperation};
Object g_listing_operation = {g_async_vtable, 1, kListingOperationKind};
Object g_unfulfilled_operation = {g_async_vtable, 1,
                                  kUnfulfilledConsumablesOperation};

}  // namespace

Object* BooleanOperation() noexcept {
  return &g_boolean_operation;
}

Object* NullResultOperation() noexcept {
  return &g_null_operation;
}

Object* SpeechOperation() noexcept {
  return &g_speech_operation;
}

Object* ListingOperation() noexcept {
  return &g_listing_operation;
}

HRESULT SHIM_COM GetLicenseInformation(Object* self, void** out) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(&g_license, out);
  if (SUCCEEDED(result)) {
    log::Write("CurrentApp LicenseInformation returned");
  }
  return result;
}

HRESULT SHIM_COM RequestReceiptAsync(Object* self, void** operation) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(&g_receipt_operation, operation);
  if (SUCCEEDED(result)) {
    log::Write("CurrentApp receipt async operation returned");
  }
  return result;
}

HRESULT SHIM_COM LoadListingInformationAsync(Object* self,
                                             void** operation) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(&g_listing_operation, operation);
  if (SUCCEEDED(result)) {
    log::Write("CurrentApp LoadListingInformationAsync returned");
  }
  return result;
}

HRESULT SHIM_COM GetUnfulfilledConsumablesAsync(Object* self,
                                                void** operation) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(&g_unfulfilled_operation, operation);
  if (SUCCEEDED(result)) {
    log::Write("CurrentApp GetUnfulfilledConsumablesAsync returned");
  }
  return result;
}

HRESULT SHIM_COM GetEmptyCollection(Object* self, void** out) noexcept {
  const HRESULT result = ReturnSingleton(&g_empty_collection, out);
  if (FAILED(result)) {
    return result;
  }
  // El `kind` del objeto que llama solo se usa para elegir el mensaje: la
  // coleccion devuelta es la misma en ambos casos.
  const bool is_license = self != nullptr && self->kind == kLicenseInformation;
  log::Write(is_license
                 ? "LicenseInformation ProductLicenses returned empty map"
                 : "ListingInformation ProductListings returned empty map");
  return S_OK;
}

HRESULT SHIM_COM SetAsyncCompletedHandler(Object* self, void* handler) noexcept {
  // Todas las operaciones asincronas del shim ya estan terminadas cuando se
  // devuelven, asi que registrar el manejador y llamarlo es la misma cosa. El
  // manejador no se guarda: se invoca aqui y se olvida.
  if (handler == nullptr || ::IsBadReadPtr(handler, sizeof(void*))) {
    return E_POINTER;
  }

  // El manejador es un objeto COM del juego, no del shim: hay que comprobar que
  // su vtable sea legible antes de saltar a ella.
  void* const* vtable = *static_cast<void* const* const*>(handler);
  if (vtable == nullptr || ::IsBadReadPtr(vtable, 4 * sizeof(void*))) {
    return E_POINTER;
  }

  // IDispatchedHandler y IAsyncOperationCompletedHandler comparten forma:
  // Invoke esta en el indice 3, justo detras de IUnknown.
  using InvokeFn = HRESULT(SHIM_COM*)(void*, Object*, int);
  const auto invoke = reinterpret_cast<InvokeFn>(vtable[3]);
  if (invoke == nullptr) {
    return E_POINTER;
  }

  // Estado 1 = AsyncStatus::Completed.
  const HRESULT result = invoke(handler, self, 1);
  log::WriteHResult("async completion callback", result);
  return result;
}

HRESULT SHIM_COM GetAsyncCompletedHandler(Object* self, void** out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  // Null y S_OK, no E_NOTIMPL: el manejador se invoco y se descarto, asi que la
  // respuesta honesta es "no hay ninguno registrado".
  *out = nullptr;
  return S_OK;
}

HRESULT SHIM_COM GetAsyncResults(Object* self, void** out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }

  // Un unico GetResults para todas las operaciones asincronas del shim, que
  // decide que devolver segun el `kind` del objeto. Ver la nota en store.h.
  //
  // Con `self` nulo o con un `kind` desconocido cae al ListingInformation, que
  // NO es un descuido: es el resultado que mas veces se pide, y devolver algo
  // valido es preferible a un null que el juego derreferencia sin comprobar.
  if (self != nullptr) {
    switch (self->kind) {
      case kReceiptOperation:
        log::Write("CurrentApp receipt GetResults returned non-null HSTRING");
        return shim::winrt_string::Create(kEmptyReceipt, kEmptyReceiptLength,
                                     reinterpret_cast<HSTRING*>(out));

      case kUnfulfilledConsumablesOperation:
        *out = &g_empty_vector;
        AddRef(&g_empty_vector);
        log::Write("CurrentApp unfulfilled GetResults returned empty vector");
        return S_OK;

      case kSpeechSynthesisOperation:
        // Null a proposito, no por omision: el juego comprueba el stream y se
        // salta la reproduccion si no lo hay, que es lo que se busca.
        *out = nullptr;
        log::Write("Win7 SpeechSynthesizer async GetResults returned null stream");
        return S_OK;

      case kBooleanOperation:
        // UN byte, no una palabra. El `boolean` de la ABI de WinRT ocupa un
        // byte, y escribir cuatro pisaria tres que no son nuestros.
        *reinterpret_cast<unsigned char*>(out) = 1;
        return S_OK;

      case kFilePickerOperation: {
        // El fichero elegido, o null si el usuario cancelo. Lo decide el modulo
        // de almacenamiento, que es quien sabe si el dialogo dejo algo.
        Object* picked = storage::PickerResult(self);
        *out = picked;
        if (picked != nullptr) {
          AddRef(picked);
        }
        return S_OK;
      }

      case kNullResultOperation:
        *out = nullptr;
        return S_OK;

      default:
        break;
    }
  }

  *out = &g_listing;
  AddRef(&g_listing);
  log::Write("CurrentApp listing GetResults returned ListingInformation");
  return S_OK;
}

}  // namespace shim::store
