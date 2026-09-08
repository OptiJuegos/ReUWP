#include "shim/version_patches.h"

#include <cstring>

#include "shim/app_window.h"
#include "shim/common.h"
#include "shim/custom_skin.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/input.h"
#include "shim/log.h"
#include "shim/object_access.h"
#include "shim/patch.h"
#include "shim/stdio_overrides.h"
#include "shim/x86_patch.h"
#include "shim/version_asm.h"

namespace shim::versions::v115 {
namespace {

// ---------------------------------------------------------------------------
// Minecraft 1.1.5 - hooks de UI persistentes
// ---------------------------------------------------------------------------
//
// El bootstrap original reemplaza estos dos metodos C++/CX despues de crear el
// HWND y antes de construir D3D/AppMain. Ambos parches son saltos de 10 bytes
// protegidos por la firma exacta embebida en d3dcraft.dll.

constexpr DWORD k115CustomSkinRva = 0x006D1740;
constexpr DWORD k115FullscreenRva = 0x006D55B0;

constexpr unsigned char k115CustomSkinSignature[10] = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xD8, 0xF2, 0x2A, 0x01};
constexpr unsigned char k115FullscreenSignature[10] = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xEF, 0x0C, 0x2C, 0x01};

// 62950870 -> 629509A0. El wrapper original conserva el callback en
// *(self+0x278)+0xA8 antes de abrir el selector Win32. El picker termina con
// `ret 4`, por eso una funcion __fastcall con un unico argumento de pila
// mantiene la ABI del metodo thiscall sustituido.
void __fastcall CustomSkinHook115(void* self, void* /*edx*/,
                                  void* callback) noexcept {
  if (self != nullptr &&
      !::IsBadReadPtr(object_access::Field(self, 0x278), sizeof(void*))) {
    void* owner = *reinterpret_cast<void**>(object_access::Field(self, 0x278));
    if (owner != nullptr &&
        !::IsBadWritePtr(object_access::Field(owner, 0xA8), sizeof(void*))) {
      *reinterpret_cast<void**>(object_access::Field(owner, 0xA8)) = callback;
    }
  }

  custom_skin::ShowAndInstall();

  // El epilogo del picker original notifica la finalizacion mediante vtable+8.
  void* const finish_entry = object_access::VtableEntry(callback, 8);
  if (finish_entry != nullptr) {
    using FinishFn = void(__thiscall*)(void* callback);
    reinterpret_cast<FinishFn>(finish_entry)(callback);
  }
}

// 62950970: lee el booleano/estado en this+4 y lo entrega al helper Win32
// 629512D0. SetBorderlessFullscreen() ya reproduce ese helper compartido.
void __fastcall FullscreenHook115(void* self, void* /*edx*/) noexcept {
  bool enabled = false;
  if (self != nullptr &&
      !::IsBadReadPtr(object_access::Field(self, 4), sizeof(DWORD))) {
    enabled = *reinterpret_cast<const DWORD*>(object_access::Field(self, 4)) != 0;
  }
  app_window::SetBorderlessFullscreen(enabled);
}

bool Install115UiHooks() noexcept {
  if (game::Current() != game::Version::kV1_1_5) {
    return true;
  }

  const hooks::x86::RelativeBranchPatch patches[] = {
      {k115CustomSkinRva, k115CustomSkinSignature,
       sizeof(k115CustomSkinSignature),
       reinterpret_cast<const void*>(&CustomSkinHook115),
       hooks::x86::RelativeBranch::kJump,
       "1.1.5 custom-skin hook failed"},
      {k115FullscreenRva, k115FullscreenSignature,
       sizeof(k115FullscreenSignature),
       reinterpret_cast<const void*>(&FullscreenHook115),
       hooks::x86::RelativeBranch::kJump,
       "1.1.5 fullscreen hook failed"},
  };

  const bool ok = hooks::x86::ApplyRelativeBranchPatches(
      reinterpret_cast<uintptr_t>(game::Base()), patches, CountOf(patches));
  log::Write(ok ? "1.1.5 custom-skin/fullscreen hooks installed"
                : "1.1.5 custom-skin/fullscreen hooks unavailable");
  return ok;
}


// ---------------------------------------------------------------------------
// Minecraft 1.1.5: parches pre-D3D del host Win32
//
// FUN_629557C0 instala estos cuatro cambios una vez que el HWND existe y antes
// del bring-up grafico. El primero sustituye una secuencia C++/CX por una
// llamada a sub_62954DA0; los otros tres son reemplazos de bytes fijos.
// ---------------------------------------------------------------------------

// The register-preserving adapter trampoline is version-owned in
// versions/v1_1_5/asm_hooks.cpp. This module keeps only patch policy.

constexpr unsigned char k115AdapterExpected[] = {
    0x8B, 0x07, 0x83, 0xC4, 0x08, 0x57, 0xFF, 0x50, 0x04};

constexpr unsigned char k115XboxOptionalExpected[] = {
    0x8B, 0x4D, 0x08, 0xC7, 0x45, 0xEC, 0x00, 0x00, 0x00, 0x00};
constexpr unsigned char k115XboxOptionalReplacement[] = {
    0x8B, 0x4D, 0x08, 0x31, 0xF6, 0x85, 0xC9, 0x74, 0x42, 0x90};

constexpr unsigned char k115XboxWorkItemExpected[] = {
    0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x4D, 0x08, 0x8D};
constexpr unsigned char k115XboxWorkItemReplacement[] = {
    0x31, 0xC0, 0xC2, 0x08, 0x00, 0x90, 0x90, 0x90};

constexpr unsigned char k115XboxWaitExpected[] = {
    0x0F, 0x1F, 0x44, 0x00, 0x00};
constexpr unsigned char k115XboxWaitReplacement[] = {
    0xE9, 0x0A, 0x00, 0x00, 0x00};


// Cinco callbacks Xbox que el host Win32 conserva en la UI pero convierte en
// no-op. El stub devuelve 8 y limpia el unico argumento de pila (`ret 4`), igual
// que la tabla embebida en unk_629892AC.
constexpr unsigned char k115XboxCallbackNoOp[] = {
    0xB8, 0x08, 0x00, 0x00, 0x00, 0xC2, 0x04, 0x00};
constexpr unsigned char k115XboxSaveExpected[] = {
    0x83, 0xC1, 0x04, 0xE9, 0x18, 0xF1, 0xFF, 0xFF};
constexpr unsigned char k115XboxStoreSignInExpected[] = {
    0x83, 0xC1, 0x04, 0xE9, 0xB8, 0xE7, 0xFF, 0xFF};
constexpr unsigned char k115XboxSignInAExpected[] = {
    0x83, 0xC1, 0x04, 0xE9, 0x08, 0xEE, 0xFF, 0xFF};
constexpr unsigned char k115XboxSignInBExpected[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x41, 0x04, 0x83, 0xC1};
constexpr unsigned char k115XboxSignInCExpected[] = {
    0x83, 0xC1, 0x08, 0xE9, 0x38, 0xF8, 0xFF, 0xFF};

// Los tres bloques siguientes son los guards de la caja de texto XAML. En HWND
// la interfaz puede ser nula; el original inserta el chequeo y salta las
// llamadas SetText/selection/caret cuando no existe.
constexpr unsigned char k115ChatSetTextExpected[] = {
    0x8B,0x06,0x8B,0x7D,0xD0,0x57,0x56,0xFF,0x50,0x1C,
    0x85,0xC0,0x79,0x06,0x50,0xE8,0x6B,0xDA,0x8D,0xFF};
constexpr unsigned char k115ChatSetTextReplacement[] = {
    0x8B,0x7D,0xD0,0x85,0xF6,0x74,0x1A,0x8B,0x06,0x57,
    0x56,0xFF,0x50,0x1C,0x90,0x90,0x90,0x90,0x90,0x90};
constexpr unsigned char k115ChatSelectionExpected[] = {
    0x89,0x75,0xCC,0x57,0xC6,0x45,0xFC,0x02,0x8B,0x06,
    0x57,0x56,0xFF,0x90,0x90,0x00,0x00,0x00,0x85,0xC0,
    0x79,0x06,0x50,0xE8,0xA5,0xD9,0x8D,0xFF};
constexpr unsigned char k115ChatSelectionReplacement[] = {
    0x89,0x75,0xCC,0x85,0xF6,0x74,0x1F,0x57,0xC6,0x45,
    0xFC,0x02,0x8B,0x06,0x57,0x56,0xFF,0x90,0x90,0x00,
    0x00,0x00,0x90,0x90,0x90,0x90,0x90,0x90};
constexpr unsigned char k115ChatCaretExpected[] = {
    0x89,0x75,0xCC,0xC6,0x45,0xFC,0x03,0x8B,0x06,0x57,
    0x56,0xFF,0x50,0x34,0x85,0xC0,0x79,0x06,0x50,0xE8,
    0x5C,0xD9,0x8D,0xFF};
constexpr unsigned char k115ChatCaretReplacement[] = {
    0x89,0x75,0xCC,0x85,0xF6,0x74,0x1B,0xC6,0x45,0xFC,
    0x03,0x8B,0x06,0x57,0x56,0xFF,0x50,0x34,0x90,0x90,
    0x90,0x90,0x90,0x90};


constexpr hooks::PatchSpec k115XboxChatPatches[] = {
    {0x001E1C70, k115XboxSaveExpected, k115XboxCallbackNoOp,
     sizeof(k115XboxSaveExpected),
     "Minecraft 1.1.5 save-to-Xbox callback no-op patch failed"},
    {0x002FCA30, k115XboxStoreSignInExpected, k115XboxCallbackNoOp,
     sizeof(k115XboxStoreSignInExpected),
     "Minecraft 1.1.5 Store Sign In callback no-op patch failed"},
    {0x002EA600, k115XboxSignInAExpected, k115XboxCallbackNoOp,
     sizeof(k115XboxSignInAExpected),
     "Minecraft 1.1.5 Xbox sign-in callback A no-op patch failed"},
    {0x0033F090, k115XboxSignInBExpected, k115XboxCallbackNoOp,
     sizeof(k115XboxSignInBExpected),
     "Minecraft 1.1.5 Xbox sign-in callback B no-op patch failed"},
    {0x00341970, k115XboxSignInCExpected, k115XboxCallbackNoOp,
     sizeof(k115XboxSignInCExpected),
     "Minecraft 1.1.5 Xbox sign-in callback C no-op patch failed"},
    {0x007B1021, k115ChatSetTextExpected, k115ChatSetTextReplacement,
     sizeof(k115ChatSetTextExpected),
     "Minecraft 1.1.5 HWND text-box SetText guard patch failed"},
    {0x007B10DF, k115ChatSelectionExpected, k115ChatSelectionReplacement,
     sizeof(k115ChatSelectionExpected),
     "Minecraft 1.1.5 HWND text-box selection guard patch failed"},
    {0x007B112C, k115ChatCaretExpected, k115ChatCaretReplacement,
     sizeof(k115ChatCaretExpected),
     "Minecraft 1.1.5 HWND text-box caret guard patch failed"},
};

// Guarda de nulo en el bucle de espera de game+0x3A05D0.
//
// EL FALLO
//
// Ese bucle gira sobre un singleton global (game+0x140E88C) y lo desreferencia
// sin comprobarlo:
//
//     3A05D0  mov  eax,[glob]              <- cabecera del bucle
//     3A05D5  cmp  byte [eax+0x85],0       <- AV si glob == 0
//      ...    mira los flags +0x84..+0x88
//     3A0611  call esi                     <- yield
//     3A0613  jmp  3A05D0
//     3A0615  pop  esi / ret               <- salida
//
// El global lo inicializa otro hilo. Es una carrera de orden de arranque: si el
// worker entra al bucle antes de esa inicializacion, revienta. Medido: fallaba
// ~1 de cada 3 arranques, siempre en el mismo sitio y siempre con eax=0, sobre
// un hilo creado por _beginthreadex del propio juego (sin el shim en la pila).
//
// LA FORMA DEL PARCHE
//
// Se comprueba eax y, si es nulo, se sale de la espera por la salida que ya
// tiene la funcion. Salir y no seguir girando es deliberado: se observo una
// sesion entera con el global a cero, y ahi un bucle "espera a que se
// inicialice" seria un cuelgue en vez de un crasheo. Es ademas la linea que
// sigue el original con todo lo de Xbox, que convierte en no-op lo que el host
// Win32 no puede satisfacer.
//
// El `cmp` original no cabe junto a la comprobacion en 7 bytes, asi que se
// reubica al relleno de alineado de justo antes del bucle. CUIDADO con ese
// relleno: no es codigo muerto. Al entrar en la funcion se cae por el hasta la
// cabecera, asi que su primer byte tiene que ser un salto al bucle o la primera
// vuelta ejecutaria el `cmp` con eax sin cargar todavia.
//
// Todo el parche es relativo: ni una direccion absoluta, asi que no depende de
// donde se cargue el juego.
//
//   3A05C4  eb 0a                  jmp 3A05D0     ; entrada normal -> bucle
//   3A05C6  80 b8 85 00 00 00 00   cmp byte [eax+0x85],0
//   3A05CD  eb 0d                  jmp 3A05DC     ; sigue la logica original
//
//   3A05D5  85 c0                  test eax,eax
//   3A05D7  74 3c                  je  3A0615     ; nulo -> salir de la espera
//   3A05D9  eb eb                  jmp 3A05C6     ; valido -> hacer el cmp
constexpr unsigned char k115WaitLoopPadExpected[12] = {
    0x0F, 0x1F, 0x40, 0x00, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr unsigned char k115WaitLoopPadReplacement[12] = {
    0xEB, 0x0A, 0x80, 0xB8, 0x85, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x0D, 0x90};

constexpr unsigned char k115WaitLoopHeadExpected[7] = {
    0x80, 0xB8, 0x85, 0x00, 0x00, 0x00, 0x00};
constexpr unsigned char k115WaitLoopHeadReplacement[7] = {
    0x85, 0xC0, 0x74, 0x3C, 0xEB, 0xEB, 0x90};

// Tabla propia, y en este orden. PatchTransaction revierte la tabla entera si
// una entrada falla, asi que aislarlos garantiza que se aplican los dos o
// ninguno. El relleno va primero: mientras solo este el, la entrada salta al
// bucle y el codigo original sigue intacto, o sea que el estado intermedio es
// seguro aunque un hilo pase por ahi entre las dos escrituras.
constexpr hooks::PatchSpec k115WaitLoopGuardPatches[] = {
    {0x003A05C4, k115WaitLoopPadExpected, k115WaitLoopPadReplacement,
     sizeof(k115WaitLoopPadExpected),
     "Minecraft 1.1.5 wait-loop guard pad patch failed"},
    {0x003A05D5, k115WaitLoopHeadExpected, k115WaitLoopHeadReplacement,
     sizeof(k115WaitLoopHeadExpected),
     "Minecraft 1.1.5 wait-loop guard head patch failed"},
};

constexpr hooks::PatchSpec k115OptionalPreD3DPatches[] = {
    {0x007B3069, k115XboxOptionalExpected, k115XboxOptionalReplacement,
     sizeof(k115XboxOptionalExpected),
     "Minecraft 1.1.5 optional Xbox interface patch failed"},
};

// Estos dos bloques estan en el camino observado del constructor AppMainXaml.
// Si alguno no coincide, continuar deja vivo codigo UWP/Xbox con una ABI que el
// host HWND no puede satisfacer. Por eso son prerequisitos, no best-effort.
constexpr hooks::PatchSpec k115CriticalPatches[] = {
    {0x007B34C0, k115XboxWorkItemExpected, k115XboxWorkItemReplacement,
     sizeof(k115XboxWorkItemExpected),
     "Minecraft 1.1.5 Xbox work-item no-op patch failed"},
    {0x006CDC9B, k115XboxWaitExpected, k115XboxWaitReplacement,
     sizeof(k115XboxWaitExpected),
     "Minecraft 1.1.5 Xbox initialization wait patch failed"},
};

constexpr DWORD k115AdapterRegisterRva = 0x007AB00A;

void BuildAdapterReplacement(unsigned char* target,
                             unsigned char* replacement) noexcept {
  replacement[0] = 0xE8;
  replacement[5] = 0x90;
  replacement[6] = 0x90;
  replacement[7] = 0x90;
  replacement[8] = 0x90;
  const void* const entry = asm_hooks::AdapterRegisterEntry();
  unsigned char call[5] = {};
  if (entry == nullptr ||
      !hooks::x86::BuildRelativeBranch(
          target, entry, hooks::x86::RelativeBranch::kCall, call,
          sizeof(call))) {
    memset(replacement, 0, sizeof(k115AdapterExpected));
    return;
  }
  memcpy(replacement, call, sizeof(call));
}

enum class AdapterPatchState {
  kNeedsPatch,
  kAlreadyPatched,
  kUnavailable,
};

AdapterPatchState PrepareAdapterRegisterPatch(
    hooks::PatchSpec* patch, unsigned char* replacement) noexcept {
  auto* const target =
      static_cast<unsigned char*>(game::Resolve(k115AdapterRegisterRva));
  if (target == nullptr || ::IsBadReadPtr(target, sizeof(k115AdapterExpected))) {
    log::Write("Minecraft 1.1.5 C++/CX adapter register target unavailable");
    return AdapterPatchState::kUnavailable;
  }

  BuildAdapterReplacement(target, replacement);
  if (replacement[0] != 0xE8) {
    log::Write("Minecraft 1.1.5 C++/CX adapter register trampoline unavailable");
    return AdapterPatchState::kUnavailable;
  }

  if (hooks::MatchesSignature(target, replacement, sizeof(k115AdapterExpected))) {
    log::Write("Minecraft 1.1.5 C++/CX adapter register already patched");
    return AdapterPatchState::kAlreadyPatched;
  }
  if (!hooks::MatchesSignature(target, k115AdapterExpected,
                               sizeof(k115AdapterExpected))) {
    log::Write("Minecraft 1.1.5 C++/CX adapter register signature mismatch");
    return AdapterPatchState::kUnavailable;
  }

  *patch = {k115AdapterRegisterRva, k115AdapterExpected, replacement,
            sizeof(k115AdapterExpected),
            "Minecraft 1.1.5 C++/CX adapter register patch failed"};
  return AdapterPatchState::kNeedsPatch;
}

bool RestoreAdapterRegisterPatch(const unsigned char* replacement) noexcept {
  const hooks::PatchSpec rollback_patch = {
      k115AdapterRegisterRva, replacement, k115AdapterExpected,
      sizeof(k115AdapterExpected),
      "Minecraft 1.1.5 adapter rollback failed"};
  const hooks::PatchApplyResult result = hooks::ApplyIdempotentPatch(
      reinterpret_cast<uintptr_t>(game::Base()), rollback_patch);
  return result == hooks::PatchApplyResult::kApplied ||
         result == hooks::PatchApplyResult::kAlreadyApplied;
}


}  // namespace

// Minecraft 1.1.5 usa ID3D11DeviceContext1::DiscardView en una ruta que no
// existe en Windows 7. El d3dcraft original no intenta emularla: convierte la
// funcion del juego que agrupa esas llamadas en un `ret` de un byte. Para 1.1.5
// solo acepta el prologo 0xA1 esperado o 0xC3 si ya fue parcheada.
constexpr DWORD k115DiscardViewRva = 0x006D0DA0;
constexpr unsigned char k115DiscardViewExpected[] = {0xA1};
constexpr unsigned char k115DiscardViewReplacement[] = {0xC3};
constexpr hooks::PatchSpec k115DiscardViewPatch = {
    k115DiscardViewRva, k115DiscardViewExpected, k115DiscardViewReplacement,
    sizeof(k115DiscardViewExpected),
    "Minecraft 1.1.5 Windows 7 DiscardView patch refused"};

bool InstallCompatibilityPatches() noexcept {
  if (!game::HasCapability(game::VersionCapability::kPreD3DCompatibility) ||
      !game::IsWindows7()) {
    return true;
  }

  const hooks::PatchApplyResult result = hooks::ApplyIdempotentPatch(
      reinterpret_cast<uintptr_t>(game::Base()), k115DiscardViewPatch);
  if (result == hooks::PatchApplyResult::kAlreadyApplied) {
    log::Write(
        "Minecraft 1.1.5 Windows 7 D3D11.1 DiscardView calls already disabled");
    return true;
  }
  if (result == hooks::PatchApplyResult::kApplied) {
    log::Write(
        "Minecraft 1.1.5 Windows 7 D3D11.1 DiscardView calls disabled");
    return true;
  }
  if (result == hooks::PatchApplyResult::kUnavailable) {
    log::Write(
        "Minecraft 1.1.5 Windows 7 DiscardView patch target unavailable");
    return false;
  }
  if (result == hooks::PatchApplyResult::kUnexpectedBytes) {
    log::Write(
        "Minecraft 1.1.5 Windows 7 DiscardView patch refused: unexpected function prologue");
    return false;
  }

  log::Write("Windows 7 DiscardView compatibility patch failed");
  return false;
}

bool InstallPreD3DPatches() noexcept {
  if (!game::HasCapability(game::VersionCapability::kPreD3DCompatibility)) {
    return true;
  }

  const uintptr_t module_base = reinterpret_cast<uintptr_t>(game::Base());
  const hooks::PatchTransaction transaction(module_base);

  unsigned char adapter_replacement[sizeof(k115AdapterExpected)] = {};
  hooks::PatchSpec adapter_patch = {};
  const AdapterPatchState adapter_state =
      PrepareAdapterRegisterPatch(&adapter_patch, adapter_replacement);
  if (adapter_state == AdapterPatchState::kUnavailable) {
    log::Write("Minecraft 1.1.5 critical pre-D3D adapter patch failed");
    return false;
  }

  bool critical_ok = false;
  if (adapter_state == AdapterPatchState::kAlreadyPatched) {
    critical_ok =
        transaction.Apply(k115CriticalPatches, CountOf(k115CriticalPatches));
  } else {
    const hooks::PatchSpec critical_patches[] = {
        adapter_patch,
        k115CriticalPatches[0],
        k115CriticalPatches[1],
    };
    critical_ok = transaction.Apply(critical_patches,
                                    CountOf(critical_patches));
  }
  if (!critical_ok) {
    if (adapter_state == AdapterPatchState::kAlreadyPatched &&
        !RestoreAdapterRegisterPatch(adapter_replacement)) {
      log::Write("Minecraft 1.1.5 critical pre-D3D adapter rollback failed");
    }
    log::Write("Minecraft 1.1.5 critical pre-D3D patches incomplete");
    return false;
  }

  const bool optional_ok = transaction.Apply(
      k115OptionalPreD3DPatches, CountOf(k115OptionalPreD3DPatches));
  const bool xbox_chat_ok =
      transaction.Apply(k115XboxChatPatches, CountOf(k115XboxChatPatches));

  // Opcional a proposito: si la firma no cuadra, no aplicarlo deja el juego
  // exactamente como estaba, que es el comportamiento de siempre. Abortar el
  // arranque por esto seria peor que la carrera que evita.
  const bool wait_loop_ok = transaction.Apply(
      k115WaitLoopGuardPatches, CountOf(k115WaitLoopGuardPatches));
  log::Write(wait_loop_ok
                 ? "Minecraft 1.1.5 wait-loop null guard installed"
                 : "Minecraft 1.1.5 wait-loop null guard unavailable");

  if (!optional_ok || !xbox_chat_ok || !wait_loop_ok) {
    log::Write("Minecraft 1.1.5 optional pre-D3D patches incomplete");
  }

  log::Write("Minecraft 1.1.5 critical pre-D3D C++/CX/Xbox patches installed");
  return true;
}

void DumpAdapterSnapshotForced() noexcept {
  if (!game::HasCapability(game::VersionCapability::kPreD3DCompatibility)) {
    return;
  }

  const asm_hooks::AdapterSnapshot snapshot =
      asm_hooks::GetAdapterSnapshot();
  log::WritefForced(
      "1.1.5 adapter snapshot: fn=%08lx self=%08lx arg0=%08lx",
      static_cast<unsigned long>(snapshot.function),
      static_cast<unsigned long>(snapshot.self),
      static_cast<unsigned long>(snapshot.arg0));
  log::WritefForced("  arg1=%08lx arg2=%08lx",
                    static_cast<unsigned long>(snapshot.arg1),
                    static_cast<unsigned long>(snapshot.arg2));

  const auto* target =
      static_cast<const unsigned char*>(game::Resolve(0x007AB00A));
  if (target != nullptr && !::IsBadReadPtr(target, sizeof(k115AdapterExpected))) {
    log::WritefForced(
        "  adapter bytes=%02x %02x %02x %02x %02x %02x %02x %02x %02x",
        target[0], target[1], target[2], target[3], target[4], target[5],
        target[6], target[7], target[8]);
  }
}


void InstallPostWindowPatches() noexcept {
  stdio_overrides::InstallStdioHooks();
  input::InstallHooks();
  Install115UiHooks();
}

}  // namespace shim::versions::v115
