#include "shim/log.h"

#include <stdarg.h>

#include "shim/branding.h"
#include "shim/config.h"

namespace shim::log {
namespace {

// dword_62993000 en el original.
//
// No hay sincronizacion: varios hilos pueden resolver la variable a la vez,
// pero todos escriben el mismo valor, asi que la carrera es benigna. Se
// mantiene igual que el original en lugar de "arreglarlo" con un atomico,
// porque anadir una barrera cambiaria el codigo generado en un camino que se
// ejecuta cientos de veces por frame.
State g_state = State::kUnknown;

// El original repite este par de llamadas en cada punto de traza en vez de
// pasar por una funcion comun; la segunda emite el salto de linea.
void Emit(const char* message) noexcept {
  ::OutputDebugStringA(message);
  ::OutputDebugStringA("\n");
}

}  // namespace

bool IsEnabled() noexcept {
#if !REUWP_ENABLE_LOGGING
  return false;
#else
  State state = g_state;
  if (static_cast<int>(state) < 0) {
    const DWORD primary_length =
        ::GetEnvironmentVariableA(SHIM_DEBUG_ENV_VAR, nullptr, 0);
#if REUWP_ENABLE_COMPAT_DEBUG_ENV
    const DWORD compat_length = primary_length == 0
                                    ? ::GetEnvironmentVariableA(
                                          SHIM_COMPAT_DEBUG_ENV_VAR, nullptr, 0)
                                    : 0;
#else
    const DWORD compat_length = 0;
#endif
    state = (primary_length != 0 || compat_length != 0)
                ? State::kEnabled
                : State::kDisabled;
    g_state = state;
  }
  return state == State::kEnabled;
#endif
}

void Write(const char* message) noexcept {
#if !REUWP_ENABLE_LOGGING
  (void)message;
  return;
#else
  if (message == nullptr) {
    return;
  }
  if (!IsEnabled()) {
    return;
  }
  Emit(message);
#endif
}

void Writef(const char* format, ...) noexcept {
#if !REUWP_ENABLE_LOGGING
  (void)format;
  return;
#else
  if (format == nullptr || !IsEnabled()) {
    return;
  }

  char buffer[kFormatBufferSize];
  va_list args;
  va_start(args, format);
  ::wvsprintfA(buffer, format, args);
  va_end(args);
  Emit(buffer);
#endif
}

void WriteForced(const char* message) noexcept {
#if REUWP_ENABLE_LOGGING || REUWP_ENABLE_CRASH_REPORTING || REUWP_ENABLE_WATCHDOG
  if (message == nullptr) {
    return;
  }
  Emit(message);
#else
  (void)message;
#endif
}

void WritefForced(const char* format, ...) noexcept {
#if REUWP_ENABLE_LOGGING || REUWP_ENABLE_CRASH_REPORTING || REUWP_ENABLE_WATCHDOG
  if (format == nullptr) {
    return;
  }
  char buffer[kFormatBufferSize];
  va_list args;
  va_start(args, format);
  ::wvsprintfA(buffer, format, args);
  va_end(args);
  Emit(buffer);
#else
  (void)format;
#endif
}

void WriteHResult(const char* what, HRESULT hr) noexcept {
  Writef("%s: HRESULT 0x%08lx", what, hr);
}

void WritePointer(const char* what, const void* value) noexcept {
  Writef("%s: %p", what, value);
}

}  // namespace shim::log
