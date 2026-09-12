// Objeto base de los stubs WinRT.
//
// Casi la mitad del binario (217 de 454 funciones) son metodos de vtable de diez
// lineas o menos colgando de esta base. Reconstruirla bien es lo que hace
// manejable el resto.
//
// Layout observado (sub_6294A8A0 / A8B0 / A8E0 / A910 / A930 / A950):
//
//   +0x00  const void* vtable
//   +0x04  LONG        ref_count
//   +0x08  int         kind        etiqueta de tipo; despacha a que singleton
//                                  devolver en los getters (ver sub_6294A950)
//
// Dos propiedades del original que NO son accidentes y hay que preservar:
//
//  1. Los objetos son inmortales. Release() nunca libera: al llegar a cero
//     reinicia el contador a 1 y devuelve 1. Todos viven en .data como
//     singletons estaticos, nunca en el heap.
//  2. GetIids() devuelve cero interfaces y GetRuntimeClassName() devuelve
//     siempre la misma cadena, sin importar que clase WinRT finja ser el
//     objeto. El juego nunca los consulta, asi que el original no se molesto.

#pragma once

#include "shim/branding.h"
#include "shim/common.h"

// HSTRING, para GetRuntimeClassName.
#include <hstring.h>

namespace shim::winrt {

// Cabecera comun a todo objeto WinRT falso. Es POD a proposito: las instancias
// son globales en .data y se inicializan estaticamente, igual que en el binario.
struct Object {
  const void* vtable;
  LONG ref_count;
  int kind;
};

static_assert(sizeof(Object) == 12, "El layout del objeto WinRT es ABI fija");
static_assert(offsetof(Object, ref_count) == 4, "ref_count debe estar en +4");
static_assert(offsetof(Object, kind) == 8, "kind debe estar en +8");

// Nombre de clase que devuelven todos los objetos (configurable en branding.h).
inline constexpr wchar_t kRuntimeClassName[] = SHIM_RUNTIME_CLASS_NAME;
inline constexpr UINT32 kRuntimeClassNameLength = SHIM_RUNTIME_CLASS_NAME_LENGTH;

static_assert(CountOf(kRuntimeClassName) == kRuntimeClassNameLength + 1,
              "La longitud declarada del nombre de clase no cuadra con la "
              "cadena; se pasa explicita a WindowsCreateString.");

// ---------------------------------------------------------------------------
// Implementaciones compartidas de IInspectable
//
// Se exponen sueltas (no como metodos) porque las vtables se montan a mano como
// arrays de punteros a funcion, tal cual hace el original.
// ---------------------------------------------------------------------------

// sub_6294A8A0 - IUnknown::AddRef
ULONG SHIM_COM AddRef(Object* self) noexcept;

// sub_6294A8B0 - IUnknown::Release
//
// Deliberadamente no destruye. Ver nota (1) arriba.
ULONG SHIM_COM Release(Object* self) noexcept;

// sub_6294A8E0 - IInspectable::GetIids
//
// Devuelve S_OK con cero interfaces. Ojo: acepta punteros nulos sin fallar,
// a diferencia de GetRuntimeClassName/GetTrustLevel, que devuelven E_POINTER.
HRESULT SHIM_COM GetIids(Object* self, ULONG* iid_count, IID** iids) noexcept;

// sub_6294A910 - IInspectable::GetRuntimeClassName
HRESULT SHIM_COM GetRuntimeClassName(Object* self, HSTRING* class_name) noexcept;

// sub_6294A930 - IInspectable::GetTrustLevel
//
// Siempre BaseTrust (0).
HRESULT SHIM_COM GetTrustLevel(Object* self, int* trust_level) noexcept;

// Incrementa el contador de un singleton y lo devuelve por parametro de salida.
// Es el patron de retorno de todos los getters que ceden un objeto.
HRESULT ReturnSingleton(Object* singleton, void** out) noexcept;

}  // namespace shim::winrt
