#include "shim/runtime_trace.h"

#include "shim/log.h"
#include "shim/config.h"

namespace shim::runtime_trace {
namespace {

constexpr LONG kRecordCount = 32;

struct RecordEntry {
  volatile LONG sequence;
  BoundaryKind kind;
  const char* name;
  uintptr_t a;
  uintptr_t b;
  long result;
  DWORD thread_id;
};

RecordEntry g_records[kRecordCount] = {};
volatile LONG g_cursor = -1;

const char* KindName(BoundaryKind kind) noexcept {
  switch (kind) {
    case BoundaryKind::Async:
      return "async";
    case BoundaryKind::Fmod:
      return "fmod";
    case BoundaryKind::Storage:
      return "storage";
    default:
      return "unknown";
  }
}

}  // namespace

bool Enabled() noexcept {
#if !REUWP_ENABLE_RUNTIME_TRACE
  return false;
#else
  // Bajo x32dbg queremos conservar el ring aun cuando el usuario no haya
  // definido D3DCRAFT_DEBUG. No se emite nada por llamada: solo se vuelca si
  // ocurre un access violation, por lo que Release fuera del depurador conserva
  // el comportamiento normal.
  return log::IsEnabled() || ::IsDebuggerPresent() != FALSE;
#endif
}

void Record(BoundaryKind kind, const char* name, uintptr_t a,
            uintptr_t b, long result) noexcept {
#if !REUWP_ENABLE_RUNTIME_TRACE
  (void)kind;
  (void)name;
  (void)a;
  (void)b;
  (void)result;
  return;
#else
  const DWORD saved_last_error = ::GetLastError();
  if (!Enabled() || name == nullptr) {
    ::SetLastError(saved_last_error);
    return;
  }

  const LONG ticket = ::InterlockedIncrement(&g_cursor);
  RecordEntry& entry = g_records[static_cast<unsigned long>(ticket) %
                                 static_cast<unsigned long>(kRecordCount)];

  // sequence=0 marks the slot as being rewritten. Publish it last.
  ::InterlockedExchange(&entry.sequence, 0);
  entry.kind = kind;
  entry.name = name;
  entry.a = a;
  entry.b = b;
  entry.result = result;
  entry.thread_id = ::GetCurrentThreadId();
  ::MemoryBarrier();
  ::InterlockedExchange(&entry.sequence, ticket + 1);
  ::SetLastError(saved_last_error);
#endif
}

namespace {

void DumpRecentImpl(bool forced) noexcept {
  if (!Enabled()) {
    return;
  }

  const LONG newest = g_cursor;
  if (newest < 0) {
    if (forced) {
      log::WriteForced("runtime boundary trace: empty");
    } else {
      log::Write("runtime boundary trace: empty");
    }
    return;
  }

  if (forced) {
    log::WriteForced("runtime boundary trace (newest first):");
  } else {
    log::Write("runtime boundary trace (newest first):");
  }
  for (LONG distance = 0; distance < kRecordCount; ++distance) {
    const LONG ticket = newest - distance;
    if (ticket < 0) {
      break;
    }
    const RecordEntry& entry =
        g_records[static_cast<unsigned long>(ticket) %
                  static_cast<unsigned long>(kRecordCount)];
    const LONG sequence = entry.sequence;
    if (sequence != ticket + 1 || entry.name == nullptr) {
      continue;
    }
    if (forced) {
      log::WritefForced(
          "  #%ld %s %s tid=%lu a=%08lx b=%08lx result=%08lx", ticket,
          KindName(entry.kind), entry.name, entry.thread_id,
          static_cast<unsigned long>(entry.a),
          static_cast<unsigned long>(entry.b),
          static_cast<unsigned long>(entry.result));
    } else {
      log::Writef(
          "  #%ld %s %s tid=%lu a=%08lx b=%08lx result=%08lx", ticket,
          KindName(entry.kind), entry.name, entry.thread_id,
          static_cast<unsigned long>(entry.a),
          static_cast<unsigned long>(entry.b),
          static_cast<unsigned long>(entry.result));
    }
  }
}

}  // namespace

void DumpRecent() noexcept {
  DumpRecentImpl(false);
}

void DumpRecentForced() noexcept {
  DumpRecentImpl(true);
}

}  // namespace shim::runtime_trace
