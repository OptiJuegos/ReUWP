// Windows.Security.Cryptography.CryptographicBuffer falsificado.
//
// Original: sub_6294BAA0..BC40 (los estaticos) y sub_6294BCF0 (la fabrica de
// buffers).
//
//
// POR QUE ESTA CLASE ES DISTINTA A TODAS LAS DEMAS DEL SHIM
//
// El resto del WinRT falso son singletons estaticos de 12 bytes que viven en
// `.data` y nunca se destruyen. Un buffer criptografico no puede serlo: tiene
// tamano variable y el juego crea uno nuevo por cada operacion. Asi que este es
// el unico objeto del shim que:
//
//   - se reserva en el monton (dos veces: la cabecera y los bytes),
//   - lleva un contador de referencias DE VERDAD, y se libera al llegar a cero,
//   - expone DOS interfaces COM desde el mismo bloque de 32 bytes.
//
// Lo tercero es la parte que hay que entender bien. El objeto tiene dos vtables:
// IBuffer en el offset 0 e IBufferByteAccess en el 24. Un puntero a la segunda
// NO apunta al principio del objeto, asi que sus metodos no pueden usar `this`
// directamente; para eso esta el puntero de vuelta en el offset 28. Es el
// layout que genera un compilador de C++ para herencia multiple, escrito a mano.
//
// IBufferByteAccess es como el codigo nativo llega a los bytes crudos; sin ella
// un IBuffer solo se puede leer desde C#.

#pragma once

#include "shim/winrt_object.h"

namespace shim::winrt {

// Etiqueta de los buffers (leida del binario, sub_6294BCF0).
inline constexpr int kBufferKind = 27;

// Un buffer con sus dos interfaces. 32 bytes exactos, como en el binario.
struct Buffer {
  const void* vtable;       // +0x00  IBuffer
  LONG ref_count;           // +0x04
  int kind;                 // +0x08
  unsigned int length;      // +0x0C  bytes en uso
  unsigned int capacity;    // +0x10  bytes reservados
  unsigned char* data;      // +0x14  reserva aparte
  const void* byte_access;  // +0x18  IBufferByteAccess
  Buffer* self;             // +0x1C  vuelta al principio desde el offset 24
};

static_assert(sizeof(Buffer) == 32,
              "El layout del buffer es el del binario (sub_6294BCF0)");

// Estaticos de CryptographicBuffer, para la tabla de activacion.
Object* CryptographicBufferStatics() noexcept;

// Crea un buffer. Si `source` es nulo se rellena con bytes aleatorios.
//
// Devuelve nullptr si falla cualquiera de las dos reservas. El llamante recibe
// el objeto con una referencia ya tomada.
Buffer* CreateBuffer(unsigned int size, const void* source) noexcept;

}  // namespace shim::winrt
