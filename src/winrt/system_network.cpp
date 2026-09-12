#include "shim/system_services.h"
#include "shim/winrt_string_compat.h"

#include "system_services_internal.h"

#include <winstring.h>

#include "shim/log.h"
#include "shim/network.h"
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

// --- HostName -------------------------------------------------------------

// IHostName::get_CanonicalName / DisplayName / RawName.
//
// Las tres devuelven la misma IP. En WinRT se distinguen por si llevan corchetes
// (IPv6), puerto o nombre resuelto; con una IPv4 suelta no hay diferencia.
HRESULT SHIM_COM GetHostNameText(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  const wchar_t* address = net::LocalAddressText();
  return shim::winrt_string::Create(address,
                               static_cast<UINT32>(::lstrlenW(address)), out);
}

// IHostName::get_Type, raw 0x6294BA80: HostNameType::Ipv4 = 1.
HRESULT SHIM_COM GetHostNameType(Object* self, unsigned int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 1;
  return S_OK;
}

// IHostName::IsEqual, raw 0x6294BAA0. Tres parametros -> ret 0x0C.
HRESULT SHIM_COM HostNameIsEqual(Object* self, Object* other,
                                  unsigned char* out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = (self == other) ? 1 : 0;
  return S_OK;
}

Method g_host_name_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&GetHostNameText),  // IPInformation / RawName
    reinterpret_cast<Method>(&GetHostNameText),
    reinterpret_cast<Method>(&GetHostNameText),
    reinterpret_cast<Method>(&GetHostNameText),
    reinterpret_cast<Method>(&GetHostNameType),
    reinterpret_cast<Method>(&HostNameIsEqual),
};

Object g_host_name = {g_host_name_vtable, 1, kHostName};

// --- Colecciones ----------------------------------------------------------

// Vtable compartida por todos los vectores de un elemento.
Method g_single_vector_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&VectorGetAt),
    reinterpret_cast<Method>(&VectorGetSize),
    reinterpret_cast<Method>(&VectorIndexOf),
    reinterpret_cast<Method>(&VectorGetMany),
};

// IVectorView<T> vacio. El original (0x62993338) tiene los cuatro slots
// de coleccion aunque siempre responda cero elementos. Dejar solo los seis de
// IInspectable hace que slot 6 lea la siguiente global y salte a otra ABI.
Method g_empty_collection_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&VectorGetAt),
    reinterpret_cast<Method>(&VectorGetSize),
    reinterpret_cast<Method>(&VectorIndexOf),
    reinterpret_cast<Method>(&VectorGetMany),
};
static_assert(CountOf(g_empty_collection_vtable) == 10,
              "empty network vector must match the original 10-slot vtable");

Object g_host_name_vector = {g_single_vector_vtable, 1, kHostNameVector};
Object g_empty_collection = {g_empty_collection_vtable, 1, kEmptyCollection};

// --- ConnectionProfile ----------------------------------------------------

// GetNetworkConnectivityLevel: 3 = InternetAccess (raw 0x6294B530).
HRESULT SHIM_COM GetConnectivityLevel(Object* self, int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 3;
  return S_OK;
}

// get_ProfileName, raw 0x6294B510. El original crea una HSTRING fija de cinco
// caracteres; mantener un nombre Win32 estable es suficiente para el juego.
HRESULT SHIM_COM GetProfileName(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(L"Win32", 5, out);
}

// 0x6294B630: getter de DWORD/puntero nulo, reutilizado en varios slots.
HRESULT SHIM_COM ConnectionDwordZero(Object* self, unsigned int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

HRESULT SHIM_COM GetDataPlanStatus(Object* self, void** out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  return S_OK;
}

HRESULT SHIM_COM ConnectionBoolFalse(Object* self, unsigned char* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

// IConnectionCost, kind 14, raw vtable 0x6299330C. Slot 6 devuelve
// NetworkCostType=1; los cuatro booleanos restantes son false.
HRESULT SHIM_COM ConnectionCostType(Object* self, unsigned int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 1;
  return S_OK;
}

Method g_connection_cost_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&ConnectionCostType),  // 6  0x6294B670
    reinterpret_cast<Method>(&ConnectionBoolFalse), // 7  0x6294B650
    reinterpret_cast<Method>(&ConnectionBoolFalse), // 8
    reinterpret_cast<Method>(&ConnectionBoolFalse), // 9
    reinterpret_cast<Method>(&ConnectionBoolFalse), // 10
};
static_assert(CountOf(g_connection_cost_vtable) == 11,
              "IConnectionCost must match original 11-slot vtable");
Object g_connection_cost = {g_connection_cost_vtable, 1, kConnectionCost};

// 0x6294B550 devuelve el vector vacio singleton (kind 15).
HRESULT SHIM_COM GetNetworkNames(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(&g_empty_collection, out);
}

// 0x6294B5C0 devuelve el singleton IConnectionCost (kind 14).
HRESULT SHIM_COM GetConnectionCost(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(&g_connection_cost, out);
}

// IConnectionProfile, raw vtable 0x62993294: 15 entradas exactas.
Method g_connection_profile_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&GetProfileName),        // 6  0x6294B510
    reinterpret_cast<Method>(&GetConnectivityLevel),  // 7  0x6294B530
    reinterpret_cast<Method>(&GetNetworkNames),       // 8  0x6294B550
    reinterpret_cast<Method>(&GetConnectionCost),     // 9  0x6294B5C0
    reinterpret_cast<Method>(&GetDataPlanStatus),     // 10 0x6294B630
    reinterpret_cast<Method>(&ConnectionDwordZero),   // 11 0x6294B630
    nullptr,                                           // 12
    nullptr,                                           // 13
    reinterpret_cast<Method>(&ConnectionDwordZero),   // 14 0x6294B630
};
static_assert(CountOf(g_connection_profile_vtable) == 15,
              "IConnectionProfile must match original 15-slot vtable");
Object g_connection_profile = {g_connection_profile_vtable, 1,
                               kConnectionProfile};

// IConnectionProfile2 projection (kind 13, raw vtable 0x629932D0). Generic
// QueryInterface returns this only for IID E2045145-4C9F-400C-9150-7EC7D6E2888A.
Method g_connection_profile2_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&ConnectionBoolFalse),  // 6
    reinterpret_cast<Method>(&ConnectionBoolFalse),  // 7
    reinterpret_cast<Method>(&ConnectionDwordZero),  // 8
    reinterpret_cast<Method>(&ConnectionDwordZero),  // 9
    reinterpret_cast<Method>(&ConnectionDwordZero),  // 10
    reinterpret_cast<Method>(&ConnectionDwordZero),  // 11
    reinterpret_cast<Method>(&GetConnectivityLevel), // 12 -> 3
    nullptr,                                          // 13 raw NULL
    nullptr,                                          // 14 raw NULL
};
static_assert(CountOf(g_connection_profile2_vtable) == 15,
              "IConnectionProfile2 projection must have 15 slots");
Object g_connection_profile2 = {g_connection_profile2_vtable, 1,
                                kConnectionProfile2};

// IIterable<HostName> + IIterator<HostName>, kinds 19/20. These are separate
// from the kind-18 IVectorView returned by GetHostNames. The original QI for
// kind 18 projects to kind 19, and First() then returns kind 20.
bool g_host_iterator_consumed = false;
Object* HostIteratorSingleton() noexcept;

HRESULT SHIM_COM HostIterableFirst(Object* self, void** out) noexcept {
  (void)self;
  g_host_iterator_consumed = false;
  return ReturnSingleton(HostIteratorSingleton(), out);
}

HRESULT SHIM_COM HostIteratorCurrent(Object* self, void** out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  if (g_host_iterator_consumed) {
    return kBoundsError;
  }
  return ReturnSingleton(&g_host_name, out);
}

HRESULT SHIM_COM HostIteratorHasCurrent(Object* self,
                                        unsigned char* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = g_host_iterator_consumed ? 0 : 1;
  return S_OK;
}

HRESULT SHIM_COM HostIteratorMoveNext(Object* self,
                                      unsigned char* out) noexcept {
  (void)self;
  g_host_iterator_consumed = true;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

HRESULT SHIM_COM HostIteratorGetMany(Object* self, unsigned int capacity,
                                     void** items,
                                     unsigned int* written) noexcept {
  (void)self;
  if (written == nullptr) {
    return E_POINTER;
  }
  *written = 0;
  if (capacity == 0 || g_host_iterator_consumed) {
    return S_OK;
  }
  if (items == nullptr) {
    return E_POINTER;
  }
  items[0] = &g_host_name;
  AddRef(&g_host_name);
  g_host_iterator_consumed = true;
  *written = 1;
  return S_OK;
}

Method g_host_iterable_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&HostIterableFirst),
};
static_assert(CountOf(g_host_iterable_vtable) == 7,
              "HostNames IIterable projection must have 7 slots");
Method g_host_iterator_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&HostIteratorCurrent),
    reinterpret_cast<Method>(&HostIteratorHasCurrent),
    reinterpret_cast<Method>(&HostIteratorMoveNext),
    reinterpret_cast<Method>(&HostIteratorGetMany),
};
static_assert(CountOf(g_host_iterator_vtable) == 10,
              "HostNames IIterator projection must have 10 slots");
Object g_host_iterable = {g_host_iterable_vtable, 1, 19};
Object g_host_iterator = {g_host_iterator_vtable, 1, 20};
Object* HostIteratorSingleton() noexcept { return &g_host_iterator; }

// --- NetworkInformation ---------------------------------------------------

HRESULT SHIM_COM GetConnectionProfiles(Object* self, void** out) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(&g_empty_collection, out);
  if (SUCCEEDED(result)) {
    log::Write("Win32 NetworkInformation: empty collection returned");
  }
  return result;
}

HRESULT SHIM_COM GetInternetConnectionProfile(Object* self,
                                              void** out) noexcept {
  (void)self;
  const HRESULT result = ReturnSingleton(&g_connection_profile, out);
  if (SUCCEEDED(result)) {
    log::Write("Win32 NetworkInformation: InternetConnectionProfile returned");
  }
  return result;
}

HRESULT SHIM_COM GetHostNames(Object* self, void** out) noexcept {
  (void)self;
  // Se refresca la IP en cada consulta, no una sola vez al arrancar: el juego
  // pregunta al abrir una partida en red, y para entonces la maquina puede
  // haber cambiado de interfaz.
  net::ResolveLocalAddress();
  const HRESULT result = ReturnSingleton(&g_host_name_vector, out);
  if (SUCCEEDED(result)) {
    log::Write("Win32 NetworkInformation: local IPv4 HostName returned");
  }
  return result;
}

// Los dos metodos de perfil por adaptador: no hay adaptadores que enumerar.
HRESULT SHIM_COM GetProfileByAdapter(Object* self, void* adapter,
                                     void** out) noexcept {
  (void)self;
  (void)adapter;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  return E_NOTIMPL;
}

HRESULT SHIM_COM GetProfileByAdapterAndKind(Object* self, void* adapter,
                                            int kind, void** out) noexcept {
  (void)self;
  (void)adapter;
  (void)kind;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  return E_NOTIMPL;
}

// Registro de eventos de cambio de red. Devuelve un token a cero: no se va a
// notificar nunca, porque el shim no vigila la red.
HRESULT SHIM_COM AddNetworkStatusChanged(Object* self, void* handler,
                                         unsigned int* token) noexcept {
  (void)self;
  (void)handler;
  if (token == nullptr) {
    return E_POINTER;
  }
  // El token es de 64 bits (EventRegistrationToken), no de 32.
  token[0] = 0;
  token[1] = 0;
  return S_OK;
}

HRESULT SHIM_COM RemoveNetworkStatusChanged(Object* self, unsigned int low,
                                            unsigned int high) noexcept {
  (void)self;
  (void)low;
  (void)high;
  return S_OK;
}

// INetworkInformationStatics, contrastada con 0x6299325C: 14 entradas.
Method g_network_information_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&GetConnectionProfiles),         //  6
    reinterpret_cast<Method>(&GetInternetConnectionProfile),  //  7
    reinterpret_cast<Method>(&GetConnectionProfiles),         //  8
    reinterpret_cast<Method>(&GetHostNames),                  //  9
    reinterpret_cast<Method>(&GetProfileByAdapter),           // 10
    reinterpret_cast<Method>(&GetProfileByAdapterAndKind),    // 11
    reinterpret_cast<Method>(&AddNetworkStatusChanged),       // 12
    reinterpret_cast<Method>(&RemoveNetworkStatusChanged),    // 13
};

Object g_network_information = {g_network_information_vtable, 1,
                                kNetworkInformation};


#undef SHIM_IINSPECTABLE

}  // namespace

namespace system_detail {

Object* HostNameSingleton() noexcept { return &g_host_name; }

Object* HostNamesIterableSingleton() noexcept { return &g_host_iterable; }

Object* ConnectionProfile2Singleton() noexcept { return &g_connection_profile2; }

Object* NetworkInformationSingleton() noexcept {
  return &g_network_information;
}

}  // namespace system_detail
}  // namespace shim::winrt
