#include "shim/watchdog.h"

#include "shim/log.h"
#include "shim/config.h"

namespace shim::diag {
namespace {

// Id del hilo principal (dword_629995E4). Cero mientras el bootstrap no lo
// registre, y en ese caso el vigilante no hace nada.
DWORD g_watched_thread_id = 0;
volatile LONG g_constructor_active = 0;

// APIs de contexto de hilo, resueltas una sola vez.
//
// Se resuelven en tiempo de ejecucion en vez de importarlas porque el vigilante
// es opcional: si faltan, el shim debe seguir arrancando.
struct ThreadApi {
  HANDLE(WINAPI* OpenThread)(DWORD, BOOL, DWORD);
  DWORD(WINAPI* SuspendThread)(HANDLE);
  BOOL(WINAPI* GetThreadContext)(HANDLE, LPCONTEXT);
  DWORD(WINAPI* ResumeThread)(HANDLE);
};

ThreadApi g_api = {};

// THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME
constexpr DWORD kThreadAccess = 0x0040 | 0x0008 | 0x0002;

// CONTEXT_i386 | CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS
constexpr DWORD kContextFlags = 0x00010007;

// Tope de la cadena de EBP.
constexpr unsigned kMaxFrames = 16;

bool EnsureThreadApi() noexcept {
  if (g_api.OpenThread != nullptr) {
    return true;
  }

  const HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
  if (kernel32 == nullptr) {
    return false;
  }

  g_api.OpenThread = reinterpret_cast<decltype(g_api.OpenThread)>(
      ::GetProcAddress(kernel32, "OpenThread"));
  g_api.SuspendThread = reinterpret_cast<decltype(g_api.SuspendThread)>(
      ::GetProcAddress(kernel32, "SuspendThread"));
  g_api.GetThreadContext = reinterpret_cast<decltype(g_api.GetThreadContext)>(
      ::GetProcAddress(kernel32, "GetThreadContext"));
  g_api.ResumeThread = reinterpret_cast<decltype(g_api.ResumeThread)>(
      ::GetProcAddress(kernel32, "ResumeThread"));

  // Las cuatro o ninguna: suspender sin poder reanudar dejaria el juego colgado
  // para siempre, que es peor que no vigilar.
  return g_api.OpenThread != nullptr && g_api.SuspendThread != nullptr &&
         g_api.GetThreadContext != nullptr && g_api.ResumeThread != nullptr;
}

struct StackFrame {
  const StackFrame* caller;
  const void* return_address;
};

void DumpFrames(const CONTEXT& context) noexcept {
  const auto* frame = reinterpret_cast<const StackFrame*>(context.Ebp);
  for (unsigned depth = 0; depth < kMaxFrames; ++depth) {
    if (frame == nullptr || ::IsBadReadPtr(frame, sizeof(StackFrame))) {
      return;
    }
    log::Writef("  watchdog frame %u: return=%08X frame=%08X%s", depth,
                frame->return_address, frame, "");

    const StackFrame* next = frame->caller;
    // La pila crece hacia abajo: un marco que no sube significa cadena rota.
    if (reinterpret_cast<uintptr_t>(next) <=
        reinterpret_cast<uintptr_t>(frame)) {
      return;
    }
    frame = next;
  }
  log::Write(" (iteration limit reached)");
}

bool ConstructorActive() noexcept {
  return ::InterlockedCompareExchange(&g_constructor_active, 0, 0) != 0;
}

DWORD WINAPI WatchdogThread(LPVOID) noexcept {
  ::Sleep(kFirstSnapshotDelayMs);
  if (ConstructorActive()) {
    log::Write("Minecraft 1.1.5 watchdog: AppMainXaml still active after 3s");
    CaptureThreadSnapshot("3-second constructor stall");
  }

  ::Sleep(kSecondSnapshotDelayMs - kFirstSnapshotDelayMs);
  if (ConstructorActive()) {
    log::Write("Minecraft 1.1.5 watchdog: AppMainXaml still active after 10s");
    CaptureThreadSnapshot("10-second constructor stall");
  }
  return 0;
}

}  // namespace

void SetWatchedThread(DWORD thread_id) noexcept {
#if REUWP_ENABLE_WATCHDOG
  g_watched_thread_id = thread_id;
#else
  (void)thread_id;
#endif
}

void SetConstructorActive(bool active) noexcept {
#if REUWP_ENABLE_WATCHDOG
  ::InterlockedExchange(&g_constructor_active, active ? 1 : 0);
#else
  (void)active;
#endif
}

void CaptureThreadSnapshot(const char* label) noexcept {
#if !REUWP_ENABLE_WATCHDOG
  (void)label;
  return;
#else
  if (g_watched_thread_id == 0) {
    log::Write("watchdog: main thread id is unavailable");
    return;
  }
  if (!EnsureThreadApi()) {
    log::Write("watchdog: thread-context APIs unavailable");
    return;
  }

  const HANDLE thread =
      g_api.OpenThread(kThreadAccess, FALSE, g_watched_thread_id);
  if (thread == nullptr) {
    log::Write("watchdog: OpenThread failed");
    return;
  }

  if (g_api.SuspendThread(thread) == static_cast<DWORD>(-1)) {
    log::Write("watchdog: SuspendThread failed");
    ::CloseHandle(thread);
    return;
  }

  // A partir de aqui el hilo principal esta congelado. Nada de reservar
  // memoria ni tomar cerrojos hasta reanudarlo: si quedo suspendido dentro del
  // heap, cualquier reserva aqui interbloquea el proceso entero.
  CONTEXT context;
  memset(&context, 0, sizeof(context));
  context.ContextFlags = kContextFlags;

  if (g_api.GetThreadContext(thread, &context)) {
    log::Writef(
        "watchdog %s: EIP=%08X ESP=%08X EBP=%08X EAX=%08X ECX=%08X EDX=%08X",
        label != nullptr ? label : "snapshot", context.Eip, context.Esp,
        context.Ebp, context.Eax, context.Ecx, context.Edx);
    DumpFrames(context);
  } else {
    log::Write("watchdog: GetThreadContext failed");
  }

  g_api.ResumeThread(thread);
  ::CloseHandle(thread);
#endif
}

bool ArmConstructorWatchdog() noexcept {
#if !REUWP_ENABLE_WATCHDOG
  return false;
#else
  if (g_watched_thread_id == 0) {
    log::Write("watchdog: main thread id is unavailable");
    return false;
  }

  DWORD thread_id = 0;
  const HANDLE thread =
      ::CreateThread(nullptr, 0, &WatchdogThread, nullptr, 0, &thread_id);
  if (thread == nullptr) {
    log::Write("constructor watchdog CreateThread failed");
    return false;
  }

  // El handle se cierra ya: el hilo vive por su cuenta y nadie lo espera.
  ::CloseHandle(thread);
  log::Write("constructor watchdog armed for 3s/10s snapshots");
  return true;
#endif
}

}  // namespace shim::diag
