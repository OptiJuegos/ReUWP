// Parcheo de ficheros de texto orientado a lineas.
//
// Original: sub_62949370.
//
// Opera sobre ficheros clave=valor (options.txt, .lang y similares): busca una
// linea que empiece por cierto prefijo y la sustituye entera, o la anade al
// final si no aparece.
//
// Trabaja sobre bytes, no sobre texto decodificado, asi que es agnostico a la
// codificacion mientras sea compatible con ASCII (UTF-8 incluido).

#pragma once

#include "shim/common.h"

namespace shim::util {

// Buffer devuelto por ReplaceLine. La memoria sale del heap del proceso y la
// libera el llamante con Release().
struct PatchedText {
  void* data = nullptr;
  size_t size = 0;

  // Libera el buffer. Seguro de llamar varias veces o sobre uno vacio.
  void Release() noexcept;
};

// Sustituye la linea que empieza por `key_prefix` por `replacement_line`.
//
// El emparejamiento exige que `key_prefix` caiga al inicio de una linea: al
// principio del buffer, justo despues de un '\n', o en el offset 3 si el fichero
// arranca con BOM UTF-8 (EF BB BF). Asi una clave no puede colar por aparecer a
// media linea.
//
// El llamante pasa el prefijo *con* su separador y la linea de reemplazo
// completa. Por ejemplo: key_prefix = "gfx_fullscreen=" y
// replacement_line = "gfx_fullscreen=1".
//
// Si el prefijo no aparece, `replacement_line` se anade al final, precedido de
// CRLF si el contenido no terminaba ya en salto de linea. Las lineas anadidas
// siempre acaban en CRLF.
//
// Devuelve false si `key_prefix` o `replacement_line` son nulos, o si falla la
// reserva de memoria. En caso de exito, `out` recibe un buffer nuevo que el
// llamante debe liberar.
//
// Nota: replica al original en no validar `text`. Un puntero nulo con `size`
// distinto de cero es un fallo del llamante y peta, igual que en el binario.
bool ReplaceLine(const void* text,
                 size_t size,
                 const void* key_prefix,
                 size_t key_prefix_size,
                 const void* replacement_line,
                 size_t replacement_line_size,
                 PatchedText* out) noexcept;

}  // namespace shim::util
