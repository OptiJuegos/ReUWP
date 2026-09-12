// Construccion de vtables COM a mano.
//
// Las vtables del shim son arrays de punteros a funcion en datos mutables, no
// vtables generadas por el compilador. Se hace asi porque es lo que hace el
// original (viven en .data, no en .rdata) y porque algunas se parchean segun la
// version del juego.
//
// Cada vtable empieza obligatoriamente por los seis metodos de IInspectable en
// este orden, que es ABI fija de COM/WinRT:
//
//   0  QueryInterface
//   1  AddRef
//   2  Release
//   3  GetIids
//   4  GetRuntimeClassName
//   5  GetTrustLevel
//
// A partir del 6 van los de la interfaz concreta, en el orden en que los declara
// su .idl. Ese orden NO es negociable: el juego llama por indice.

#pragma once

#include "shim/winrt_object.h"

namespace shim::winrt {

// Puntero generico de metodo de vtable. El cast a la firma real es
// responsabilidad de quien la declara.
using Method = void*;

// Numero de metodos que aporta IInspectable antes de los de la interfaz.

// QueryInterface compartida (raw 0x6294A660; IDA la agrupa como sub_6294A640).
//
// Reproduce las proyecciones especiales del original por `kind`: colecciones
// enumerables, IConnectionProfile2 y, en 1.1.5, IAsyncInfo. Solo los objetos
// sin una proyeccion especial caen al fallback que devuelve `self`.
HRESULT SHIM_COM QueryInterface(Object* self, const IID* iid,
                                void** out) noexcept;

// Stub exacto de 0x6294E040. El original lo usa en unos pocos slots
// deliberadamente no implementados (StorageFile/FilePicker):
//
//   mov eax, 0x80004001  ; E_NOTIMPL
//   ret                  ; SIN limpiar argumentos
//
// Es __cdecl y no recibe parametros a proposito. No se puede sustituir por un
// metodo __stdcall de aridad estimada: eso cambia ESP si el slot se invoca.
HRESULT __cdecl NotImplementedNoCleanup() noexcept;

}  // namespace shim::winrt
