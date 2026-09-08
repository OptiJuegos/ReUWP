// Traza de diagnostico, controlada por una variable de entorno.
//
// Original: sub_62947610 (texto plano) y sub_62947660 (texto + HRESULT).
//
// Las variantes con formato aparecen ~250 veces inlineadas en el binario como
// `wsprintfA(buf, "...", ...)` seguido del test de la variable; eran helpers en
// el codigo original y aqui se restauran como tales.

#pragma once

#include "shim/common.h"

namespace shim::log {

// Estado de la traza, cacheado tras la primera consulta.
//
// Corresponde a dword_62993000. El tri-estado importa: el binario distingue
// "sin consultar" (<0) de "desactivado" (0), de modo que GetEnvironmentVariableA
// se llama una unica vez aunque la variable no exista.
enum class State : int {
  kUnknown = -1,
  kDisabled = 0,
  kEnabled = 1,
};

// True si la variable de traza esta definida (con cualquier valor, incluso
// vacio: el binario solo comprueba que la longitud devuelta sea distinta de
// cero). El nombre de la variable se configura en branding.h.
bool IsEnabled() noexcept;

// Emite una linea por OutputDebugStringA. El salto de linea va en una segunda
// llamada, tal cual hace el original.
//
// Nota de reconstruccion: IDA muestra ese segundo argumento como
// L"\nWindows.UI.Core.CoreWindow". Es un artefacto: los bytes en 0x6298FE0C son
// `0A 00`, o sea "\n" en ANSI, y el decompilador lo interpreto como wide
// solapandolo con la cadena contigua.
void Write(const char* message) noexcept;

// Traza con formato. El original usa wsprintfA con buffer en pila de 164 bytes;
// se conserva ese limite porque wsprintfA no acepta tamano y truncar distinto
// cambiaria la salida observable.
void Writef(const char* format, ...) noexcept;

// Diagnostico forzado para el manejador de access violations. Estas variantes
// ignore the configured debug environment variables and are used only when a debugger is already attached
// to the process; they do not change the normal DLL logging flow.
void WriteForced(const char* message) noexcept;
void WritefForced(const char* format, ...) noexcept;

// Helpers reconstruidos a partir de los patrones inlineados mas frecuentes.
void WriteHResult(const char* what, HRESULT hr) noexcept;  // "%s: HRESULT 0x%08lx"
void WritePointer(const char* what, const void* value) noexcept;  // "%s: %p"

// Tamano del buffer de formato en pila usado por el original.
inline constexpr size_t kFormatBufferSize = 164;

}  // namespace shim::log
