#include "shim/patch.h"

#include <string.h>

#include "shim/log.h"

namespace shim::hooks {
namespace {

// Invalida la cache de instrucciones del proceso para un rango.
//
// Imprescindible tras tocar codigo: sin esto el procesador puede seguir
// ejecutando bytes ya sustituidos, y el fallo resultante es intermitente y
// practicamente imposible de diagnosticar.
void FlushCode(const void* address, size_t size) noexcept {
  ::FlushInstructionCache(::GetCurrentProcess(), address, size);
}

// Deshace las `applied` primeras entradas de una tabla, en orden inverso.
//
// No hace falta haber guardado los bytes originales: cada entrada se aplico
// solo despues de comprobar que en destino estaban exactamente los de
// `expected`, asi que devolver ese buffer restaura el estado de partida.
//
// Si una reversion falla no queda nada que hacer salvo dejar constancia: el
// modulo se queda inconsistente y forzar mas escrituras solo lo empeoraria.
void RollBack(uintptr_t module_base, const PatchSpec* patches,
              size_t applied) noexcept {
  for (size_t index = applied; index > 0; --index) {
    const PatchSpec& patch = patches[index - 1];
    auto* target = reinterpret_cast<void*>(module_base + patch.rva);
    if (!WriteCode(target, patch.expected, patch.size)) {
      log::Writef("patch rollback failed at rva=%08lx", patch.rva);
    }
  }
}

}  // namespace

bool MatchesSignature(const void* address, const void* expected,
                      size_t size) noexcept {
  return memcmp(address, expected, size) == 0;
}

bool WriteCode(void* address, const void* bytes, size_t size) noexcept {
  DWORD original_protection = 0;
  if (!::VirtualProtect(address, size, PAGE_EXECUTE_READWRITE,
                        &original_protection)) {
    return false;
  }

  memcpy(address, bytes, size);

  // La proteccion se restaura siempre, incluso si algo fuera mal despues: dejar
  // paginas de codigo escribibles es justo lo que buscan los exploits.
  DWORD discarded = 0;
  ::VirtualProtect(address, size, original_protection, &discarded);

  FlushCode(address, size);
  return true;
}

bool PatchTransaction::Apply(const PatchSpec* patches,
                             size_t count) const noexcept {
  if (count == 0) {
    return true;
  }
  if (module_base_ == 0 || patches == nullptr) {
    return false;
  }

  for (size_t index = 0; index < count; ++index) {
    const PatchSpec& patch = patches[index];
    auto* target = reinterpret_cast<void*>(module_base_ + patch.rva);

    // La firma se comprueba ANTES de tocar nada. Es lo que impide que un
    // parche pensado para 1.1.5 corrompa el codigo de 0.15.10.
    if (MatchesSignature(target, patch.expected, patch.size) &&
        WriteCode(target, patch.replacement, patch.size)) {
      continue;
    }

    log::Write(patch.failure_message);

    // DIVERGENCIA DELIBERADA respecto al original: se revierte lo ya aplicado.
    //
    // El binario sale sin deshacer nada, y eso deja el juego en un estado que no
    // corresponde a ninguna version: unos parches puestos y otros no. Como los
    // parches de una tabla suelen ser interdependientes (desactivar una llamada
    // exige haber redirigido antes su destino), ese estado a medias produce
    // fallos mucho peores de diagnosticar que no parchear en absoluto.
    //
    // Revertir no necesita guardar nada: se comprobo que los bytes coincidian
    // con `expected` justo antes de escribir, asi que restaurar es volver a
    // poner `expected`.
    RollBack(module_base_, patches, index);
    return false;
  }
  return true;
}

PatchApplyResult ApplyIdempotentPatch(uintptr_t module_base,
                                      const PatchSpec& patch) noexcept {
  if (module_base == 0 || patch.expected == nullptr ||
      patch.replacement == nullptr || patch.size == 0) {
    return PatchApplyResult::kUnavailable;
  }

  void* const target = reinterpret_cast<void*>(module_base + patch.rva);
  if (::IsBadReadPtr(target, patch.size)) {
    return PatchApplyResult::kUnavailable;
  }
  if (MatchesSignature(target, patch.replacement, patch.size)) {
    return PatchApplyResult::kAlreadyApplied;
  }
  if (!MatchesSignature(target, patch.expected, patch.size)) {
    return PatchApplyResult::kUnexpectedBytes;
  }
  if (!WriteCode(target, patch.replacement, patch.size)) {
    return PatchApplyResult::kWriteFailed;
  }
  return PatchApplyResult::kApplied;
}

bool ApplyCodePatches(uintptr_t module_base, const CodePatch* patches,
                      size_t count) noexcept {
  return PatchTransaction(module_base).Apply(patches, count);
}

bool ReplacePointer(void** slot, const void* replacement, void** previous,
                    const char* name) noexcept {
  DWORD original_protection = 0;

  // PAGE_READWRITE, no PAGE_EXECUTE_READWRITE: la IAT son datos.
  if (!::VirtualProtect(slot, sizeof(void*), PAGE_READWRITE,
                        &original_protection)) {
    log::Writef("hook %s: VirtualProtect failed", name);
    return false;
  }

  if (previous != nullptr) {
    *previous = *slot;
  }
  *slot = const_cast<void*>(replacement);

  DWORD discarded = 0;
  ::VirtualProtect(slot, sizeof(void*), original_protection, &discarded);
  FlushCode(slot, sizeof(void*));

  log::Writef("hook installed: %s original=%p", name,
              previous != nullptr ? *previous : nullptr);
  return true;
}

bool ReplaceImportRva(HMODULE module, DWORD rva, const void* replacement,
                      void** previous, const char* name) noexcept {
  if (module == nullptr || rva == 0) {
    return false;
  }

  auto* const slot = reinterpret_cast<void**>(
      reinterpret_cast<unsigned char*>(module) + rva);
  return ReplacePointer(slot, replacement, previous, name);
}

}  // namespace shim::hooks
