#include "shim/crash_report.h"

#include "shim/log.h"
#include "shim/config.h"
#include "shim/runtime_trace.h"
#include "shim/compat_hooks.h"
#include "shim/version_patches.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/patch.h"

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#endif

namespace shim::diag {
namespace {

// dword_6299602C en el original. Cero mientras el bootstrap no localice el
// modulo del juego; en ese caso ningun frame se anota como suyo.
uintptr_t g_game_module_base = 0;
uintptr_t g_reuwp_module_base = 0;
size_t g_reuwp_module_span = 0;

struct AppMainAbiSnapshot115 {
  volatile LONG valid;
  uintptr_t entry;
  uintptr_t out_slot;
  uintptr_t string_argument;
  unsigned long string_capacity;
  uintptr_t aux0_slot;
  uintptr_t aux1_slot;
  uintptr_t aux2_slot;
  uintptr_t aux0_value;
  uintptr_t aux1_value;
  uintptr_t aux2_value;
  DWORD thread_id;
};

AppMainAbiSnapshot115 g_app_main_abi_115 = {};

using CopyFn = void* (__cdecl*)(void*, const void*, size_t);
CopyFn g_original_memcpy = nullptr;
CopyFn g_original_memmove = nullptr;
volatile LONG g_copy_probe_active_115 = 0;
constexpr size_t kSuspiciousCopySize115 = 0x00100000;  // 1 MiB

// Codigos que atiende el manejador vectorizado. El resto se deja pasar.
constexpr DWORD kMsvcCppException = 0xE06D7363;  // el literal 'msc'
constexpr DWORD kStackBufferOverrun = 0xC0000409;  // __fastfail

// Volcado de pila del filtro final: 48 bytes desde ESP, de cuatro en cuatro.
constexpr size_t kStackDumpBytes = 48;

// Tope de la cadena de EBP. Sin el, una pila corrupta haria bucle infinito.
constexpr unsigned kMaxFrameWalkDepth = 16;

// Tope de RtlCaptureStackBackTrace en el VEH.
constexpr ULONG kMaxCapturedFrames = 64;

// Ventana del VEH: 0x40 bytes antes de ESP y 0xC0 despues = 0x100 bytes.
// Es suficientemente grande para ver el retorno/argumentos del CRT sin hacer
// un volcado enorme dentro de un manejador de excepciones.
constexpr size_t kVehStackBeforeBytes = 0x40;
constexpr size_t kVehStackAfterBytes = 0xC0;

bool InRange(uintptr_t value, uintptr_t base, size_t span) noexcept {
  return base != 0 && span != 0 && value >= base && value < base + span;
}

void CacheSelfModuleRange() noexcept {
  MEMORY_BASIC_INFORMATION mbi = {};
  if (::VirtualQuery(reinterpret_cast<const void*>(&InstallHandlers), &mbi,
                     sizeof(mbi)) != sizeof(mbi) ||
      mbi.AllocationBase == nullptr) {
    return;
  }

  const auto base = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (::IsBadReadPtr(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE) {
    return;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (::IsBadReadPtr(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE) {
    return;
  }

  g_reuwp_module_base = base;
  g_reuwp_module_span = nt->OptionalHeader.SizeOfImage;
}

void Dump115AppMainAbiSnapshotForced() noexcept {
  if (::InterlockedCompareExchange(&g_app_main_abi_115.valid, 0, 0) == 0) {
    return;
  }
  const AppMainAbiSnapshot115 snapshot = g_app_main_abi_115;
  log::WritefForced(
      "1.1.5 AppMain ABI snapshot: tid=%lu entry=%08lx out=%08lx str=%08lx cap=%lu",
      snapshot.thread_id, static_cast<unsigned long>(snapshot.entry),
      static_cast<unsigned long>(snapshot.out_slot),
      static_cast<unsigned long>(snapshot.string_argument),
      snapshot.string_capacity);
  log::WritefForced(
      "  aux slots=%08lx/%08lx/%08lx values=%08lx/%08lx/%08lx",
      static_cast<unsigned long>(snapshot.aux0_slot),
      static_cast<unsigned long>(snapshot.aux1_slot),
      static_cast<unsigned long>(snapshot.aux2_slot),
      static_cast<unsigned long>(snapshot.aux0_value),
      static_cast<unsigned long>(snapshot.aux1_value),
      static_cast<unsigned long>(snapshot.aux2_value));
}

void DumpVehStackWindow(const CONTEXT* context) noexcept {
  const uintptr_t esp = context->Esp;
  log::WriteForced("VEH stack window around ESP (0x100 bytes):");
  for (long offset = -static_cast<long>(kVehStackBeforeBytes);
       offset < static_cast<long>(kVehStackAfterBytes);
       offset += static_cast<long>(sizeof(DWORD))) {
    const bool negative = offset < 0;
    const unsigned long offset_magnitude = static_cast<unsigned long>(
        negative ? -offset : offset);
    const char* const offset_sign = negative ? "-" : "+";
    const uintptr_t slot_address =
        negative ? esp - static_cast<uintptr_t>(offset_magnitude)
                 : esp + static_cast<uintptr_t>(offset_magnitude);
    const auto* slot = reinterpret_cast<const DWORD*>(slot_address);
    if (::IsBadReadPtr(slot, sizeof(*slot))) {
      log::WritefForced("  stack[%s%lu] @%08lx = <unreadable>", offset_sign,
                        offset_magnitude,
                        static_cast<unsigned long>(slot_address));
      continue;
    }

    const uintptr_t value = *slot;
    if (InRange(value, g_game_module_base, kGameModuleSpan)) {
      log::WritefForced(
          "  stack[%s%lu] @%08lx = %08lx -> game+%08lx", offset_sign,
          offset_magnitude, static_cast<unsigned long>(slot_address),
          static_cast<unsigned long>(value),
          static_cast<unsigned long>(value - g_game_module_base));
    } else if (InRange(value, g_reuwp_module_base,
                       g_reuwp_module_span)) {
      log::WritefForced(
          "  stack[%s%lu] @%08lx = %08lx -> reuwp+%08lx", offset_sign,
          offset_magnitude, static_cast<unsigned long>(slot_address),
          static_cast<unsigned long>(value),
          static_cast<unsigned long>(value - g_reuwp_module_base));
    } else {
      log::WritefForced("  stack[%s%lu] @%08lx = %08lx", offset_sign,
                        offset_magnitude,
                        static_cast<unsigned long>(slot_address),
                        static_cast<unsigned long>(value));
    }
  }
}

void DumpCopyCandidate(const EXCEPTION_RECORD* record,
                       const CONTEXT* context) noexcept {
  const ULONG_PTR access_type = record->ExceptionInformation[0];
  const uintptr_t inaccessible = record->ExceptionInformation[1];
  if (access_type != 0 || context->Esi != inaccessible ||
      context->Edx < context->Ecx) {
    return;
  }

  const uintptr_t processed = context->Edx - context->Ecx;
  if (processed > context->Esi || processed > context->Edi) {
    return;
  }

  const uintptr_t source_initial = context->Esi - processed;
  const uintptr_t destination_initial = context->Edi - processed;
  const bool source_above_ebp = source_initial >= context->Ebp;
  const unsigned long source_ebp_delta = static_cast<unsigned long>(
      source_above_ebp ? source_initial - context->Ebp
                       : context->Ebp - source_initial);
  log::WritefForced(
      "copy candidate (heuristic): processed=%08lx count.initial=%08lx",
      static_cast<unsigned long>(processed), context->Edx);
  log::WritefForced(
      "  source.initial=%08lx destination.initial=%08lx source-ebp=%s%lu",
      static_cast<unsigned long>(source_initial),
      static_cast<unsigned long>(destination_initial),
      source_above_ebp ? "+" : "-", source_ebp_delta);
}

// Etiqueta de procedencia de una direccion de retorno.
const char* ModuleTag(const void* address) noexcept {
  if (g_game_module_base == 0) {
    return "";
  }
  const auto value = reinterpret_cast<uintptr_t>(address);
  if (value < g_game_module_base ||
      value >= g_game_module_base + kGameModuleSpan) {
    return "";
  }
  return " (game)";
}

// Describe the mapped module containing an address.  Used only while
// Debug tracing is enabled; failure to resolve a module is itself useful
// evidence (for example, a wild write target).
void LogAddressModule(const char* label, const void* address) noexcept {
  MEMORY_BASIC_INFORMATION mbi = {};
  if (address == nullptr ||
      ::VirtualQuery(address, &mbi, sizeof(mbi)) != sizeof(mbi) ||
      mbi.AllocationBase == nullptr) {
    log::WritefForced("  %s=%p module=<unmapped>", label, address);
    return;
  }

  const HMODULE module = static_cast<HMODULE>(mbi.AllocationBase);
  wchar_t wide_path[MAX_PATH] = {};
  char path[MAX_PATH] = {};
  if (::GetModuleFileNameW(module, wide_path, CountOf(wide_path)) != 0) {
    ::WideCharToMultiByte(CP_ACP, 0, wide_path, -1, path,
                          static_cast<int>(CountOf(path)), nullptr, nullptr);
  }
  const uintptr_t value = reinterpret_cast<uintptr_t>(address);
  const uintptr_t base = reinterpret_cast<uintptr_t>(module);
  log::WritefForced("  %s=%p module=%08lx rva=%08lx path=%s", label, address,
              static_cast<unsigned long>(base),
              static_cast<unsigned long>(value - base),
              path[0] != '\0' ? path : "<unknown>");
}

void LogLargeCopy115(const char* name, const void* destination,
                     const void* source, size_t size,
                     uintptr_t caller) noexcept {
  if (::InterlockedCompareExchange(&g_copy_probe_active_115, 0, 0) == 0 ||
      size < kSuspiciousCopySize115) {
    return;
  }

  log::WritefForced(
      "1.1.5 copy-probe large %s tid=%lu caller=%08lx dst=%08lx src=%08lx size=%08lx",
      name, ::GetCurrentThreadId(), static_cast<unsigned long>(caller),
      static_cast<unsigned long>(reinterpret_cast<uintptr_t>(destination)),
      static_cast<unsigned long>(reinterpret_cast<uintptr_t>(source)),
      static_cast<unsigned long>(size));
  LogAddressModule("copy-caller", reinterpret_cast<const void*>(caller));
}

void* __cdecl MemcpyProbe115(void* destination, const void* source,
                             size_t size) noexcept {
#if defined(_MSC_VER)
  const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
#else
  const uintptr_t caller = 0;
#endif
  LogLargeCopy115("memcpy", destination, source, size, caller);
  return g_original_memcpy != nullptr
             ? g_original_memcpy(destination, source, size)
             : destination;
}

void* __cdecl MemmoveProbe115(void* destination, const void* source,
                              size_t size) noexcept {
#if defined(_MSC_VER)
  const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
#else
  const uintptr_t caller = 0;
#endif
  LogLargeCopy115("memmove", destination, source, size, caller);
  return g_original_memmove != nullptr
             ? g_original_memmove(destination, source, size)
             : destination;
}

bool GetIatSlots(uintptr_t module_base, void*** slots,
                 size_t* slot_count) noexcept {
  if (module_base == 0 || slots == nullptr || slot_count == nullptr) {
    return false;
  }

  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module_base);
  if (::IsBadReadPtr(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE) {
    return false;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
      module_base + static_cast<uintptr_t>(dos->e_lfanew));
  if (::IsBadReadPtr(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IAT) {
    return false;
  }

  const IMAGE_DATA_DIRECTORY& iat =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
  if (iat.VirtualAddress == 0 || iat.Size < sizeof(void*)) {
    return false;
  }

  auto** const first = reinterpret_cast<void**>(module_base + iat.VirtualAddress);
  const size_t count = iat.Size / sizeof(void*);
  if (::IsBadReadPtr(first, count * sizeof(void*))) {
    return false;
  }
  *slots = first;
  *slot_count = count;
  return true;
}

unsigned HookCopyTargetInModule(uintptr_t module_base, void* target,
                                const void* replacement, CopyFn* original,
                                const char* hook_name) noexcept {
  if (target == nullptr || replacement == nullptr || original == nullptr) {
    return 0;
  }

  void** slots = nullptr;
  size_t slot_count = 0;
  if (!GetIatSlots(module_base, &slots, &slot_count)) {
    return 0;
  }

  // Publicamos el destino antes de tocar la primera ranura: si otro hilo usa la
  // importacion justo despues del WritePointer, el wrapper ya puede encadenar.
  *original = reinterpret_cast<CopyFn>(target);
  unsigned patched = 0;
  for (size_t i = 0; i < slot_count; ++i) {
    if (slots[i] != target) {
      continue;
    }
    void* previous = nullptr;
    if (hooks::ReplacePointer(&slots[i], replacement, &previous, hook_name)) {
      if (previous != nullptr) {
        *original = reinterpret_cast<CopyFn>(previous);
      }
      ++patched;
    }
  }
  return patched;
}

unsigned InstallCopyProbeForModule(uintptr_t module_base, void* memcpy_target,
                                   void* memmove_target) noexcept {
  unsigned patched = HookCopyTargetInModule(
      module_base, memcpy_target, reinterpret_cast<const void*>(&MemcpyProbe115),
      &g_original_memcpy, "1.1.5 memcpy diagnostic IAT");

  // Algunas versiones del CRT hacen alias de memcpy y memmove. Si ambos nombres
  // resuelven al mismo destino, la primera pasada ya cubrio todas esas ranuras y
  // mantener un unico wrapper conserva exactamente la funcion original.
  if (memmove_target != memcpy_target) {
    patched += HookCopyTargetInModule(
        module_base, memmove_target,
        reinterpret_cast<const void*>(&MemmoveProbe115), &g_original_memmove,
        "1.1.5 memmove diagnostic IAT");
  } else if (memmove_target != nullptr) {
    g_original_memmove = reinterpret_cast<CopyFn>(memmove_target);
  }
  return patched;
}

void LogAccessViolation(const EXCEPTION_RECORD* record,
                        const CONTEXT* context) noexcept {
  const ULONG_PTR access_type = record->ExceptionInformation[0];
  const ULONG_PTR inaccessible = record->ExceptionInformation[1];
  const char* access_name = "read";
  if (access_type == 1) {
    access_name = "write";
  } else if (access_type == 8) {
    access_name = "execute";
  }

  log::WritefForced("VEH access violation: %s address=%08lx eip=%08lx",
              access_name, static_cast<unsigned long>(inaccessible),
              context->Eip);
  log::WritefForced("  crash tid=%lu", ::GetCurrentThreadId());
  log::WritefForced("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx esp=%08lx ebp=%08lx",
              context->Eax, context->Ebx, context->Ecx, context->Edx,
              context->Esi, context->Edi, context->Esp, context->Ebp);
  LogAddressModule("fault-eip", reinterpret_cast<const void*>(context->Eip));
  LogAddressModule("access-target", reinterpret_cast<const void*>(inaccessible));
  DumpCopyCandidate(record, context);
  DumpVehStackWindow(context);
  versions::v115::DumpAdapterSnapshotForced();
  Dump115AppMainAbiSnapshotForced();
  runtime_trace::DumpRecentForced();
}

// Marco de pila x86 clasico: [0] = EBP del llamante, [1] = direccion de retorno.
struct StackFrame {
  const StackFrame* caller;
  const void* return_address;
};

// Recorre la cadena de EBP volcando cada marco.
//
// Dos guardas, ambas del original: se comprueba que el marco sea legible, y se
// exige que el siguiente este MAS ALTO en memoria. La pila crece hacia abajo,
// asi que un puntero que no sube significa cadena corrupta o ciclo.
void WalkFrames(const StackFrame* frame) noexcept {
  for (unsigned depth = 0; depth < kMaxFrameWalkDepth; ++depth) {
    if (frame == nullptr || ::IsBadReadPtr(frame, sizeof(StackFrame))) {
      return;
    }
    log::WritefForced("  crash frame %u: return=%08lx frame=%08lx", depth,
                      frame->return_address, frame);

    const StackFrame* next = frame->caller;
    if (reinterpret_cast<uintptr_t>(next) <=
        reinterpret_cast<uintptr_t>(frame)) {
      return;
    }
    frame = next;
  }
}

}  // namespace

void SetGameModuleBase(uintptr_t base) noexcept {
#if !REUWP_ENABLE_CRASH_REPORTING
  (void)base;
  return;
#else
  g_game_module_base = base;

  // Mapa de modulos, forzado y una sola vez.
  //
  // Sin esto un informe de crasheo no se puede leer en frio: las direcciones de
  // la pila salen en absoluto y ambos modulos se reubican, asi que un
  // `528d5d60` no dice nada al releerlo mas tarde ni al pegarlo en un issue.
  // Con el mapa delante, cualquier traza es autocontenida.
  CacheSelfModuleRange();
  log::WritefForced("module map: game=%08lx+%08lx reuwp=%08lx+%08lx",
                    static_cast<unsigned long>(base),
                    static_cast<unsigned long>(kGameModuleSpan),
                    static_cast<unsigned long>(g_reuwp_module_base),
                    static_cast<unsigned long>(g_reuwp_module_span));
#endif
}

void Set115AppMainAbiSnapshot(uintptr_t entry, uintptr_t out_slot,
                              uintptr_t string_argument,
                              unsigned long string_capacity,
                              uintptr_t aux0_slot, uintptr_t aux1_slot,
                              uintptr_t aux2_slot, uintptr_t aux0_value,
                              uintptr_t aux1_value,
                              uintptr_t aux2_value) noexcept {
#if !REUWP_ENABLE_CRASH_REPORTING
  (void)entry;
  (void)out_slot;
  (void)string_argument;
  (void)string_capacity;
  (void)aux0_slot;
  (void)aux1_slot;
  (void)aux2_slot;
  (void)aux0_value;
  (void)aux1_value;
  (void)aux2_value;
  return;
#else
  ::InterlockedExchange(&g_app_main_abi_115.valid, 0);
  g_app_main_abi_115.entry = entry;
  g_app_main_abi_115.out_slot = out_slot;
  g_app_main_abi_115.string_argument = string_argument;
  g_app_main_abi_115.string_capacity = string_capacity;
  g_app_main_abi_115.aux0_slot = aux0_slot;
  g_app_main_abi_115.aux1_slot = aux1_slot;
  g_app_main_abi_115.aux2_slot = aux2_slot;
  g_app_main_abi_115.aux0_value = aux0_value;
  g_app_main_abi_115.aux1_value = aux1_value;
  g_app_main_abi_115.aux2_value = aux2_value;
  g_app_main_abi_115.thread_id = ::GetCurrentThreadId();
  ::MemoryBarrier();
  ::InterlockedExchange(&g_app_main_abi_115.valid, 1);
#endif
}

bool Install115MemoryCopyProbe() noexcept {
#if !REUWP_ENABLE_CRASH_REPORTING
  return false;
#else
  if (!game::HasCapability(game::VersionCapability::kAppMain115Diagnostics)) {
    return true;
  }

  CacheSelfModuleRange();
  HMODULE runtime = ::GetModuleHandleW(L"VCRUNTIME140.dll");
  if (runtime == nullptr) {
    log::Write("1.1.5 copy-probe: VCRUNTIME140.dll is not loaded");
    return false;
  }

  void* const memcpy_target = reinterpret_cast<void*>(
      ::GetProcAddress(runtime, "memcpy"));
  void* const memmove_target = reinterpret_cast<void*>(
      ::GetProcAddress(runtime, "memmove"));
  log::Writef("1.1.5 copy-probe CRT memcpy=%p memmove=%p",
              memcpy_target, memmove_target);

  if (memcpy_target == nullptr && memmove_target == nullptr) {
    return false;
  }

  const uintptr_t game_base = reinterpret_cast<uintptr_t>(game::Base());
  const unsigned game_slots =
      InstallCopyProbeForModule(game_base, memcpy_target, memmove_target);
  const unsigned shim_slots = InstallCopyProbeForModule(
      g_reuwp_module_base, memcpy_target, memmove_target);
  log::Writef("1.1.5 copy-probe installed game_slots=%lu shim_slots=%lu",
              static_cast<unsigned long>(game_slots),
              static_cast<unsigned long>(shim_slots));
  return game_slots != 0 || shim_slots != 0;
#endif
}

void Set115MemoryCopyProbeActive(bool active) noexcept {
#if REUWP_ENABLE_CRASH_REPORTING
  ::InterlockedExchange(&g_copy_probe_active_115, active ? 1 : 0);
#else
  (void)active;
#endif
}

LONG WINAPI UnhandledExceptionTrace(EXCEPTION_POINTERS* info) noexcept {
  if (info == nullptr || info->ExceptionRecord == nullptr ||
      info->ContextRecord == nullptr) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  const EXCEPTION_RECORD* record = info->ExceptionRecord;
  const CONTEXT* context = info->ContextRecord;

  // Forzado a proposito: este es el ultimo aliento del proceso. Un manejador de
  // A crash handler that only reports while debug tracing is enabled is not useful,
  // porque activar esa variable enciende tambien la traza de stdio/JSON, que
  // emite decenas de miles de lineas y altera los tiempos lo bastante como para
  // esconder las carreras. Esto no cuesta nada en ejecucion normal: solo se
  // ejecuta cuando ya no hay vuelta atras.
  log::WritefForced(
      "unhandled exception 0x%08lx at %p eip=%08lx esp=%08lx ebp=%08lx",
      record->ExceptionCode, record->ExceptionAddress, context->Eip,
      context->Esp, context->Ebp);
  log::WritefForced(
      "  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx",
      context->Eax, context->Ebx, context->Ecx, context->Edx, context->Esi,
      context->Edi);

  // Volcado crudo del tope de pila. Suele bastar para reconocer los argumentos
  // de la llamada que reventó.
  const auto* stack = reinterpret_cast<const unsigned char*>(context->Esp);
  if (!::IsBadReadPtr(stack, kStackDumpBytes)) {
    for (size_t offset = 0; offset < kStackDumpBytes; offset += sizeof(DWORD)) {
      log::WritefForced("  crash stack +%02x: %08lx", offset,
                        *reinterpret_cast<const DWORD*>(stack + offset));
    }
  }

  WalkFrames(reinterpret_cast<const StackFrame*>(context->Ebp));

  // Solo observa: deja que el sistema siga buscando manejador.
  return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI VectoredExceptionTrace(EXCEPTION_POINTERS* info) noexcept {
  if (info == nullptr || info->ExceptionRecord == nullptr ||
      info->ContextRecord == nullptr) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  const EXCEPTION_RECORD* record = info->ExceptionRecord;
  const CONTEXT* context = info->ContextRecord;

  // Filtrar aqui es esencial: un VEH ve TODAS las excepciones del proceso.
  // Access violations are added only while debug tracing is active and are
  // observed without changing exception dispatch.
  const DWORD code = record->ExceptionCode;
  if (code == EXCEPTION_ACCESS_VIOLATION && runtime_trace::Enabled()) {
    LogAccessViolation(record, context);
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (code != kMsvcCppException && code != kStackBufferOverrun) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  log::Writef(
      "VEH exception 0x%08lx at %p EAX=%p ECX=%p EDX=%p EDI=%p EBP=%p ESP=%p",
      code, record->ExceptionAddress, context->Eax, context->Ecx, context->Edx,
      context->Edi, context->Ebp, context->Esp);

  // A diferencia del filtro final, aqui se usa el desenrollado del sistema en
  // vez de la cadena de EBP: el VEH salta antes de que nada haya tocado la
  // pila, asi que RtlCaptureStackBackTrace da una traza fiable.
  PVOID frames[kMaxCapturedFrames];
  const USHORT captured =
      ::RtlCaptureStackBackTrace(0, kMaxCapturedFrames, frames, nullptr);
  for (USHORT index = 0; index < captured; ++index) {
    log::Writef("  VEH frame[%u] = %p%s", index, frames[index],
                ModuleTag(frames[index]));
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

void InstallHandlers() noexcept {
#if !REUWP_ENABLE_CRASH_REPORTING
  return;
#else
  CacheSelfModuleRange();

  // Se resuelven en tiempo de ejecucion en vez de importarlas: el shim debe
  // arrancar aunque falten, y en Win7 bajo ciertas configuraciones el import
  // estatico de AddVectoredExceptionHandler complica la carga.
  const HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
  if (kernel32 == nullptr) {
    return;
  }

  using SetUnhandledExceptionFilterFn =
      LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI*)(LPTOP_LEVEL_EXCEPTION_FILTER);
  using AddVectoredExceptionHandlerFn =
      PVOID(WINAPI*)(ULONG, PVECTORED_EXCEPTION_HANDLER);

  const auto set_filter = reinterpret_cast<SetUnhandledExceptionFilterFn>(
      ::GetProcAddress(kernel32, "SetUnhandledExceptionFilter"));
  if (set_filter != nullptr) {
    set_filter(&UnhandledExceptionTrace);
  }

  const auto add_veh = reinterpret_cast<AddVectoredExceptionHandlerFn>(
      ::GetProcAddress(kernel32, "AddVectoredExceptionHandler"));
  if (add_veh != nullptr) {
    // El 1 lo pone primero en la cadena, por delante de cualquier manejador que
    // instale el juego.
    add_veh(1, &VectoredExceptionTrace);
    log::Write("C++ exception trace installed");
  }
#endif
}

}  // namespace shim::diag
