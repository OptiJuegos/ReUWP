// Stubs de respuesta fija sobre plataforma, idioma y region.
//
// Original: sub_6294C340 (ApiInformation), sub_6294DC80 y sub_6294DCF0
// (AnalyticsInfo), sub_6294C440 (ApplicationLanguages), sub_6294C5F0
// (GeographicRegion).
//
// Ninguno consulta nada del sistema: todos devuelven un valor constante. El
// juego los usa para decidir de que plataforma cree que es, y el shim quiere
// que crea siempre lo mismo.

#pragma once

#include "shim/winrt_object.h"

namespace shim::winrt {

// Familia de dispositivo que se reporta.
inline constexpr wchar_t kDeviceFamily[] = L"Windows.Desktop";
inline constexpr UINT32 kDeviceFamilyLength = 15;

// DeviceFamilyVersion, en el formato de cadena decimal que espera WinRT.
//
// El valor empaqueta (major << 48) | (minor << 32) | (build << 16) | revision,
// asi que 2814750710366208 es 10.0.14393.0: Windows 10 build 14393. Se anuncia
// esa version en vez de la real porque es la que el juego espera de su epoca;
// decir la verdad (Windows 7) le haria tomar caminos que el shim no cubre.
inline constexpr wchar_t kDeviceFamilyVersion[] = L"2814750710366208";
inline constexpr UINT32 kDeviceFamilyVersionLength = 16;

// Idioma y region que se reportan, fijos.

Object* AnalyticsVersionInfoSingleton() noexcept;

// AnalyticsInfo::VersionInfo (sub_6294DC80)
HRESULT SHIM_COM GetAnalyticsVersionInfo(Object* factory, void** out) noexcept;

// AnalyticsVersionInfo::DeviceFamily (sub_6294DCF0)
HRESULT SHIM_COM GetDeviceFamily(Object* self, HSTRING* out) noexcept;

// AnalyticsVersionInfo::DeviceFamilyVersion (sub_6294DD50)
HRESULT SHIM_COM GetDeviceFamilyVersion(Object* self, HSTRING* out) noexcept;

Object* ApiInformationSingleton() noexcept;

// ---------------------------------------------------------------------------
// IApiInformationStatics
//
// Los diez metodos responden SIEMPRE false, para cualquier consulta. Es la
// respuesta correcta aqui: si el juego pregunta si una API existe, es porque
// tiene un camino alternativo para cuando no esta, y ese camino alternativo es
// justo el que el shim sabe atender. Decir que si le llevaria a llamar cosas
// que no existen.
//
// Hay TRES funciones y no una porque las aridades de la interfaz son
// 3, 4, 5, 4, 4, 4, 4, 4, 4, 5. Bajo __stdcall la pila la limpia el llamado, asi
// que cada hueco necesita un stub con su numero exacto de parametros. El binario
// tiene las mismas tres variantes (sub_6294C300, sub_6294C320 y sub_6294C340 /
// sub_6294C3C0), y no es casualidad.
//
// El parametro de salida es `unsigned char`, no `bool`: el `boolean` de la ABI
// de WinRT ocupa un byte y escribir mas pisaria memoria ajena.
// ---------------------------------------------------------------------------

// IsTypePresent (sub_6294C300)
HRESULT SHIM_COM IsTypePresent(Object* self, HSTRING type_name,
                               unsigned char* out) noexcept;

// IsMethodPresent, IsEventPresent, IsPropertyPresent, IsReadOnlyPropertyPresent,
// IsWriteablePropertyPresent, IsEnumNamedValuePresent e
// IsApiContractPresentByMajor (sub_6294C320 y sub_6294C3A0).
//
// Siete huecos distintos con la misma forma; el original tambien los comparte.
HRESULT SHIM_COM IsMemberPresent(Object* self, HSTRING type_name,
                                 HSTRING member_name,
                                 unsigned char* out) noexcept;

// IsMethodPresentWithArity (sub_6294C340)
HRESULT SHIM_COM IsMethodPresentWithArity(Object* self, HSTRING type_name,
                                          HSTRING method_name,
                                          unsigned int arity,
                                          unsigned char* out) noexcept;

// IsApiContractPresentByMajorAndMinor (sub_6294C3C0)
HRESULT SHIM_COM IsApiContractPresentWithMinor(Object* self,
                                               HSTRING contract_name,
                                               unsigned short major,
                                               unsigned short minor,
                                               unsigned char* out) noexcept;

}  // namespace shim::winrt
