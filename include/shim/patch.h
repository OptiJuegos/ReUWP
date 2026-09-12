// Parcheo de codigo y de punteros en caliente.
//
// Original: sub_6294F6E0 (tabla de parches de codigo) y sub_6294F7C0
// (sustitucion de un puntero suelto, tipicamente un hueco de la IAT).
//
// Son las primitivas sobre las que se apoyan todos los parches especificos de
// version del juego.

#pragma once

#include "shim/common.h"

namespace shim::hooks {

// Una entrada de la tabla de parches de codigo.
//
// El layout replica el del original: 20 bytes por entrada, recorridos con
// aritmetica de punteros. Se declara como struct para no perder de vista que
// eran registros reales, no un array plano.
struct PatchSpec {
  // Desplazamiento desde la base del modulo, no direccion absoluta: el juego se
  // reubica y estos valores se sacaron de un volcado estatico.
  uint32_t rva;

  // Bytes que DEBEN estar ahi para aplicar el parche. Es la salvaguarda contra
  // parchear una version del juego que no toca: si la firma no cuadra, se
  // aborta en vez de corromper codigo ajeno.
  const void* expected;

  // Bytes a escribir. Del mismo tamano que `expected`.
  const void* replacement;

  // Tamano en bytes de `expected` y `replacement`.
  uint32_t size;

  // Mensaje a trazar si esta entrada falla. Puede ser nulo.
  const char* failure_message;
};

static_assert(sizeof(PatchSpec) == 20, "El layout de la tabla es el del binario");

// Compatibility name for code that has not migrated to the Stage 5 API yet.
using CodePatch = PatchSpec;

enum class PatchApplyResult {
  kApplied,
  kAlreadyApplied,
  kUnavailable,
  kUnexpectedBytes,
  kWriteFailed,
};

class PatchTransaction {
 public:
  explicit PatchTransaction(uintptr_t module_base) noexcept
      : module_base_(module_base) {}

  bool Apply(const PatchSpec* patches, size_t count) const noexcept;

 private:
  uintptr_t module_base_;
};

// Applies one fixed patch while accepting replacement bytes already present in
// the image. This is intended for executable images that may ship pre-patched.
PatchApplyResult ApplyIdempotentPatch(uintptr_t module_base,
                                      const PatchSpec& patch) noexcept;

// Aplica una tabla de parches sobre un modulo cargado, de forma atomica.
//
// Se detiene en el primer fallo, sea por firma que no cuadra o por
// VirtualProtect, y REVIERTE las entradas ya aplicadas. O se aplica la tabla
// entera, o el modulo se queda como estaba.
//
// Es una divergencia deliberada respecto al original, que sale sin deshacer
// nada. Los parches de una tabla suelen ser interdependientes, y un estado a
// medias produce fallos bastante peores de diagnosticar que no parchear.
//
// Devuelve true solo si se aplicaron todas las entradas.
bool ApplyCodePatches(uintptr_t module_base, const CodePatch* patches,
                      size_t count) noexcept;

// Escribe bytes sobre codigo ejecutable, gestionando proteccion y cache.
//
// Hace las tres cosas que no se pueden olvidar al parchear codigo en x86:
// abrir la pagina a escritura, restaurar la proteccion original, e invalidar la
// cache de instrucciones. Sin lo tercero, un procesador puede seguir ejecutando
// los bytes viejos.
bool WriteCode(void* address, const void* bytes, size_t size) noexcept;

// Sustituye un puntero, tipicamente un hueco de la tabla de importacion.
//
// A diferencia de WriteCode, abre la pagina como PAGE_READWRITE y no como
// PAGE_EXECUTE_READWRITE: la IAT son datos, no codigo, y no conviene dejarla
// ejecutable ni un instante.
//
// `previous` recibe el valor anterior, que es lo que permite encadenar al
// original desde el hook.
bool ReplacePointer(void** slot, const void* replacement, void** previous,
                    const char* name) noexcept;

// Replaces an import/data pointer addressed by RVA from a loaded module base.
// A zero module or RVA is treated as an unavailable optional import.
bool ReplaceImportRva(HMODULE module, DWORD rva, const void* replacement,
                      void** previous, const char* name) noexcept;

// Comprueba que en `address` estan exactamente esos bytes.
//
// Util para validar un prologo antes de parchear cuando el parche no pasa por
// ApplyCodePatches.
bool MatchesSignature(const void* address, const void* expected,
                      size_t size) noexcept;

}  // namespace shim::hooks
