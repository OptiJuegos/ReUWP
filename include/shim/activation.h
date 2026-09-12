// Redireccion de la activacion de clases WinRT.
//
// Original: sub_62949D90, instalada sobre un hueco de la IAT del juego durante
// Win32Bootstrap ("WinRT Package/ApplicationData redirect installed").
//
// Es la pieza que hace que todo lo demas sea alcanzable. Sin esto el juego pide
// `Windows.UI.Core.CoreWindow` al sistema, el sistema no lo tiene (no hay
// paquete UWP registrado) y la peticion falla; con esto la peticion vuelve al
// shim y recibe el objeto reconstruido.
//
//
// QUE FUNCION SE ENGANCHA, EXACTAMENTE
//
// NO es RoActivateInstance ni RoGetActivationFactory. Las dos reciben un HSTRING
// como primer parametro, y un HSTRING es un handle a una cabecera, no un puntero
// al texto: compararlo con lstrcmpW no funcionaria nunca.
//
// El ensamblador del original pasa el primer parametro TAL CUAL a lstrcmpW, sin
// desreferenciar y sin llamar a WindowsGetStringRawBuffer. Es un PCWSTR crudo.
// Con eso, mas la aridad (3 argumentos, __stdcall), el segundo parametro usado
// como IID de 16 bytes, el hecho de que devuelve una FABRICA y no una instancia,
// y que el hueco de la IAT esta pegado a los otros parches de vccorlib, la
// funcion es:
//
//     Platform::Details::GetActivationFactoryByPCWSTR(
//         void* class_name, Platform::Guid& iid, void** factory)
//
// que es lo que el compilador de C++/CX emite para cada `ref new` de un tipo de
// Windows. El juego esta compilado con C++/CX, asi que TODA su activacion pasa
// por ahi: enganchar esa unica funcion cubre el modulo entero.
//
// (Deducido de la ABI, no leido de un simbolo: el binario no trae nombres de
// importacion para este hueco. Todo lo comprobable encaja.)

#pragma once

#include "shim/common.h"

#include <hstring.h>

namespace shim::winrt {

// Firma de la funcion de vccorlib que se sustituye.
//
// El IID llega por puntero, no por valor: en C++/CX es una referencia a
// Platform::Guid, que en la ABI de 32 bits viaja como puntero.
using ActivationFactoryFn = HRESULT(SHIM_COM*)(const wchar_t* class_name,
                                               const GUID* iid,
                                               void** factory) noexcept;

// Guarda la funcion original para poder encadenar.
//
// Sin ella, cualquier clase que el shim no conozca devolveria un error en vez de
// llegar al sistema. En Windows 8 y posteriores muchas SI existen de verdad, y
// dejarlas pasar es lo correcto.
void SetActivationFallback(ActivationFactoryFn original) noexcept;

// Sustituto de GetActivationFactoryByPCWSTR (sub_62949D90).
//
// Devuelve la fabrica del shim para las clases que reconoce, y encadena al
// original para el resto. Nunca falla por su cuenta: si no hay original y la
// clase no se reconoce, devuelve REGDB_E_CLASSNOTREG, que es lo que el juego
// espera cuando una clase no esta disponible.
HRESULT SHIM_COM GetActivationFactoryByName(const wchar_t* class_name,
                                            const GUID* iid,
                                            void** factory) noexcept;

// Standard WinRT activation entry points use HSTRING class names. These helpers
// deliberately resolve only ReUWP-owned projections so they remain safe when
// vccorlib itself is redirected back into the shim.
HRESULT GetActivationFactoryByHString(HSTRING class_name, const GUID* iid,
                                      void** factory) noexcept;
HRESULT ActivateInstanceByHString(HSTRING class_name, void** instance) noexcept;

// Crea las fabricas y deja la tabla lista. La llama el arranque tras crear el
// HWND y antes de instalar el gancho.
void InitializeActivation() noexcept;

}  // namespace shim::winrt
