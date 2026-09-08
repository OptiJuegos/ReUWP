#include "shim/platform_info.h"
#include "shim/winrt_string_compat.h"

#include <winstring.h>

#include "shim/log.h"
#include "shim/winrt_vtable.h"

namespace shim::winrt {
namespace {

// Etiqueta de tipo del singleton de AnalyticsVersionInfo.
constexpr int kAnalyticsVersionInfoKind = 66;

Method g_analytics_version_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&GetDeviceFamily),
    reinterpret_cast<Method>(&GetDeviceFamilyVersion),
};

Object g_analytics_version = {g_analytics_version_vtable, 1,
                              kAnalyticsVersionInfoKind};

// Etiqueta del singleton de ApiInformation (objeto 0x629976F8 del binario).
constexpr int kApiInformationKind = 31;

// IApiInformationStatics, contrastada con la vtable del binario en 0x629934A8:
// 16 entradas, diez metodos detras de IInspectable.
//
// TODOS responden "no esta presente" sin mirar lo que se les pregunta, y eso es
// lo que hace funcionar el shim entero: el juego se queda en sus caminos de
// codigo antiguos y no pide ni una sola API de Windows 10.
//
// Lo que NO se puede compartir es la implementacion, porque las aridades son
// 3, 4, 5, 4, 4, 4, 4, 4, 4, 5. Bajo __stdcall la limpia de pila la hace el
// llamado: meter el stub de 3 parametros en un hueco de 4 descuadra la pila en
// 4 bytes y el proceso se cae mas tarde, lejos de la causa. El binario tiene
// exactamente estas tres variantes por el mismo motivo.
Method g_api_information_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&IsTypePresent),              // 1 cadena
    reinterpret_cast<Method>(&IsMemberPresent),            // 2 cadenas
    reinterpret_cast<Method>(&IsMethodPresentWithArity),   // 2 cadenas + aridad
    reinterpret_cast<Method>(&IsMemberPresent),            // IsEventPresent
    reinterpret_cast<Method>(&IsMemberPresent),            // IsPropertyPresent
    reinterpret_cast<Method>(&IsMemberPresent),            // IsReadOnlyProperty
    reinterpret_cast<Method>(&IsMemberPresent),            // IsWriteableProperty
    reinterpret_cast<Method>(&IsMemberPresent),            // IsEnumNamedValue
    reinterpret_cast<Method>(&IsMemberPresent),            // ApiContract (major)
    reinterpret_cast<Method>(&IsApiContractPresentWithMinor),
};

Object g_api_information = {g_api_information_vtable, 1, kApiInformationKind};

}  // namespace

Object* ApiInformationSingleton() noexcept {
  return &g_api_information;
}

Object* AnalyticsVersionInfoSingleton() noexcept {
  return &g_analytics_version;
}

HRESULT SHIM_COM GetAnalyticsVersionInfo(Object* factory, void** out) noexcept {
  (void)factory;
  const HRESULT result = ReturnSingleton(AnalyticsVersionInfoSingleton(), out);
  if (SUCCEEDED(result)) {
    log::Write("AnalyticsInfo.VersionInfo returned Win32 version object");
  }
  return result;
}

HRESULT SHIM_COM GetDeviceFamily(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  log::Write("AnalyticsVersionInfo.DeviceFamily returned Windows.Desktop");
  return shim::winrt_string::Create(kDeviceFamily, kDeviceFamilyLength, out);
}

HRESULT SHIM_COM GetDeviceFamilyVersion(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(kDeviceFamilyVersion,
                               kDeviceFamilyVersionLength, out);
}

HRESULT SHIM_COM IsTypePresent(Object* self, HSTRING type_name,
                               unsigned char* out) noexcept {
  (void)self;
  (void)type_name;
  if (out == nullptr) {
    return E_POINTER;
  }
  // Falso para todo, sin mirar siquiera lo que se pregunta.
  //
  // `boolean` de WinRT es un byte, no el `bool` de C++ ni un BOOL de 4 bytes.
  *out = 0;
  return S_OK;
}

HRESULT SHIM_COM IsMemberPresent(Object* self, HSTRING type_name,
                                 HSTRING member_name,
                                 unsigned char* out) noexcept {
  (void)self;
  (void)type_name;
  (void)member_name;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

HRESULT SHIM_COM IsMethodPresentWithArity(Object* self, HSTRING type_name,
                                          HSTRING method_name,
                                          unsigned int arity,
                                          unsigned char* out) noexcept {
  (void)self;
  (void)type_name;
  (void)method_name;
  (void)arity;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  // El unico de los diez que deja traza, igual que en el original: es el que
  // usa el juego para decidir si existe una sobrecarga concreta, y saber cual
  // pregunto es lo unico util al depurar.
  log::Write("ApiInformation method query answered false");
  return S_OK;
}

HRESULT SHIM_COM IsApiContractPresentWithMinor(Object* self,
                                               HSTRING contract_name,
                                               unsigned short major,
                                               unsigned short minor,
                                               unsigned char* out) noexcept {
  (void)self;
  (void)contract_name;
  (void)major;
  (void)minor;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

}  // namespace shim::winrt
