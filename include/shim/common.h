// Capa de compatibilidad Win32 para Minecraft Bedrock UWP.
//
// Definiciones compartidas por todos los modulos.
//
// Reconstructed from the x86 MSVC reference binary.
//   md5    5fbf648028b919f38f17d47e815b118a
//   base   0x62940000  imagen 0x5E000
//
// La ABI de este binario no es negociable: el DLL se carga dentro del proceso
// del juego y se hace pasar por varias DLL del sistema, asi que cada convencion
// de llamada y cada layout de objeto tiene que coincidir con el original.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Restricciones de ABI
// ---------------------------------------------------------------------------

#if defined(_WIN64)
#error "El shim es x86 de 32 bits: el juego UWP objetivo es x86 y los offsets \
codificados a mano (renderer+0xF4, etc.) asumen punteros de 4 bytes."
#endif

// Los metodos COM/WinRT son __stdcall con 'this' como primer parametro
// explicito. Se declara asi (en vez de usar metodos virtuales de C++) porque
// varias vtables se construyen y parchean a mano en tiempo de ejecucion.
#define SHIM_COM __stdcall

namespace shim {

// Direccion base de la imagen en el binario original. Solo sirve para
// correlacionar esta reconstruccion con las direcciones de IDA; el codigo
// nunca debe depender de ella (el DLL se reubica).

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------

// Numero de elementos de un array en tiempo de compilacion.
template <typename T, size_t N>
constexpr size_t CountOf(const T (&)[N]) noexcept {
  return N;
}

}  // namespace shim
