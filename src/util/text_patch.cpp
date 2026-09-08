#include "shim/text_patch.h"

#include <string.h>

namespace shim::util {
namespace {

constexpr size_t kNotFound = static_cast<size_t>(-1);

// El original escribe 0x0A0D como WORD, que en little-endian son los bytes
// 0D 0A. Las lineas nuevas van con final de linea de Windows.
constexpr char kCrLf[2] = {'\r', '\n'};

bool StartsWithUtf8Bom(const unsigned char* text, size_t size) noexcept {
  return size >= 3 && text[0] == 0xEF && text[1] == 0xBB && text[2] == 0xBF;
}

// Una coincidencia solo cuenta si cae al inicio de linea: asi una clave no puede
// colar por aparecer a media linea.
bool IsLineStart(const unsigned char* text, size_t size, size_t offset) noexcept {
  if (offset == 0) {
    return true;
  }
  if (offset == 3 && StartsWithUtf8Bom(text, size)) {
    return true;
  }
  return text[offset - 1] == '\n';
}

// Offset de la primera linea que empieza por `key`, o kNotFound.
size_t FindLineStartingWith(const unsigned char* text,
                            size_t size,
                            const void* key,
                            size_t key_size) noexcept {
  if (key_size > size) {
    return kNotFound;
  }
  for (size_t offset = 0; offset + key_size <= size; ++offset) {
    if (IsLineStart(text, size, offset) &&
        memcmp(text + offset, key, key_size) == 0) {
      return offset;
    }
  }
  return kNotFound;
}

// Offset del terminador de linea que sigue a `from`, o `size` si la ultima linea
// no lo tiene.
size_t FindLineEnd(const unsigned char* text, size_t size, size_t from) noexcept {
  for (size_t offset = from; offset < size; ++offset) {
    if (text[offset] == '\n' || text[offset] == '\r') {
      return offset;
    }
  }
  return size;
}

// HeapAlloc(0) devuelve un bloque valido pero de tamano minimo. El original
// suma 1 para no pedir nunca cero; se conserva porque distingue "reserva vacia
// correcta" de "fallo de reserva" sin ambiguedad.
void* Allocate(size_t size) noexcept {
  return ::HeapAlloc(::GetProcessHeap(), 0, size != 0 ? size : 1);
}

}  // namespace

void PatchedText::Release() noexcept {
  if (data != nullptr) {
    ::HeapFree(::GetProcessHeap(), 0, data);
    data = nullptr;
  }
  size = 0;
}

bool ReplaceLine(const void* text,
                 size_t size,
                 const void* key_prefix,
                 size_t key_prefix_size,
                 const void* replacement_line,
                 size_t replacement_line_size,
                 PatchedText* out) noexcept {
  if (key_prefix == nullptr || replacement_line == nullptr || out == nullptr) {
    return false;
  }

  const auto* source = static_cast<const unsigned char*>(text);
  out->data = nullptr;
  out->size = 0;

  const size_t match =
      FindLineStartingWith(source, size, key_prefix, key_prefix_size);

  if (match != kNotFound) {
    // Se sustituye la linea entera: se conserva lo anterior, se mete la linea
    // nueva y se pega el resto desde su terminador (que asi se preserva tal
    // cual, sea LF o CRLF).
    const size_t line_end = FindLineEnd(source, size, match + key_prefix_size);
    const size_t tail_size = size - line_end;
    const size_t total = match + replacement_line_size + tail_size;

    auto* buffer = static_cast<unsigned char*>(Allocate(total));
    if (buffer == nullptr) {
      return false;
    }

    if (match != 0) {
      memcpy(buffer, source, match);
    }
    memcpy(buffer + match, replacement_line, replacement_line_size);
    if (tail_size != 0) {
      memcpy(buffer + match + replacement_line_size, source + line_end,
             tail_size);
    }

    out->data = buffer;
    out->size = total;
    return true;
  }

  // No aparece: se anade al final. Si el contenido no termina en salto de
  // linea se intercala uno, para no pegar la clave nueva a la ultima linea.
  const bool needs_separator =
      size != 0 && source[size - 1] != '\n' && source[size - 1] != '\r';
  const size_t total =
      size + (needs_separator ? sizeof(kCrLf) : 0) + replacement_line_size +
      sizeof(kCrLf);

  auto* buffer = static_cast<unsigned char*>(Allocate(total));
  if (buffer == nullptr) {
    return false;
  }

  size_t offset = 0;
  if (size != 0) {
    memcpy(buffer, source, size);
    offset = size;
  }
  if (needs_separator) {
    memcpy(buffer + offset, kCrLf, sizeof(kCrLf));
    offset += sizeof(kCrLf);
  }
  memcpy(buffer + offset, replacement_line, replacement_line_size);
  offset += replacement_line_size;
  memcpy(buffer + offset, kCrLf, sizeof(kCrLf));
  offset += sizeof(kCrLf);

  out->data = buffer;
  out->size = offset;
  return true;
}

}  // namespace shim::util
