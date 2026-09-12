// Traza de crashes: filtro de excepciones no controladas y manejador vectorizado.
//
// Original: sub_62949710 (filtro final) y sub_62949940 (VEH).
//
// Ninguno de los dos intenta recuperarse: solo vuelcan estado por la traza de
// diagnostico y dejan seguir la busqueda de manejadores. Existen porque depurar
// un juego UWP corriendo bajo un shim no es viable de otra forma: sin esto, un
// fallo dentro del juego aparece como un cierre silencioso.

#pragma once

#include "shim/common.h"

namespace shim::diag {

// Rango del modulo del juego, para anotar los frames de pila que caen dentro.
//
// El original guarda la base en dword_6299602C y asume un tamano fijo de 12 MB
// en vez de leer el tamano real de la imagen. Se conserva la aproximacion: es
// solo para etiquetar la traza, y pasarse de largo o quedarse corto no afecta a
// nada mas.
inline constexpr size_t kGameModuleSpan = 12 * 1024 * 1024;

// Registra la base del modulo del juego. La llama el bootstrap al localizarlo.
void SetGameModuleBase(uintptr_t base) noexcept;

// Conserva los argumentos crudos con los que 1.1.5 entra a game+0xAC6C0.
// El VEH los vuelca despues de un AV sin depender de que el log normal siga
// visible. Todos son valores opacos: el manejador nunca los desreferencia.
void Set115AppMainAbiSnapshot(uintptr_t entry, uintptr_t out_slot,
                              uintptr_t string_argument,
                              unsigned long string_capacity,
                              uintptr_t aux0_slot, uintptr_t aux1_slot,
                              uintptr_t aux2_slot, uintptr_t aux0_value,
                              uintptr_t aux1_value,
                              uintptr_t aux2_value) noexcept;

// Instala el filtro de excepciones no controladas y el manejador vectorizado.
//
// Resuelve SetUnhandledExceptionFilter y AddVectoredExceptionHandler en tiempo
// de ejecucion, como el original: el shim tiene que arrancar aunque falten, y
// no puede depender de su import estatico.
void InstallHandlers() noexcept;

// Diagnostico 1.1.5: intercepta solo las ranuras IAT que apuntan a
// VCRUNTIME140!memcpy/memmove, tanto en el EXE del juego como en este shim.
// No altera la semantica de la copia: los wrappers registran llamadas enormes
// durante el constructor de AppMain y encadenan inmediatamente al CRT original.
bool Install115MemoryCopyProbe() noexcept;

// Limita el logging del probe a la ventana exacta de game+0xAC6C0.
void Set115MemoryCopyProbeActive(bool active) noexcept;

// LONG WINAPI, filtro final. Vuelca contexto, 48 bytes de pila desde ESP y hasta
// 16 frames recorriendo la cadena de EBP.
//
// Siempre devuelve EXCEPTION_CONTINUE_SEARCH: solo observa.
LONG WINAPI UnhandledExceptionTrace(EXCEPTION_POINTERS* info) noexcept;

// Manejador vectorizado. Solo atiende dos codigos y deja pasar el resto:
//
//   0xE06D7363  excepcion de C++ de MSVC (el literal 'msc')
//   0xC0000409  STATUS_STACK_BUFFER_OVERRUN (__fastfail)
//
// Se ejecuta ANTES que cualquier manejador SEH del juego, asi que ve las
// excepciones de C++ que el propio juego captura y descarta.
LONG WINAPI VectoredExceptionTrace(EXCEPTION_POINTERS* info) noexcept;

}  // namespace shim::diag
