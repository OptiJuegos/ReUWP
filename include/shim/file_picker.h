// FileOpenPicker / FileSavePicker sobre los dialogos comunes de Win32.
//
// Original: sub_6294E2C0 (abrir) y sub_6294E090 (guardar).
//
// En UWP estos pickers son asincronos y devuelven un StorageFile. Aqui se
// resuelven de forma sincrona con GetOpenFileNameW/GetSaveFileNameW y el
// resultado se guarda en estado de modulo; la fachada WinRT devuelve luego un
// IAsyncOperation ya completado que lee de aqui.
//
// Este fichero contiene solo la parte de dialogo. El envoltorio COM vive en la
// capa winrt para no mezclar la logica con el pegamento de vtables.

#pragma once

#include "shim/common.h"

namespace shim::storage {

// Longitud maxima de ruta que aceptan los dialogos.
//
// El original usa 1040 WCHAR, muy por encima de MAX_PATH, para admitir rutas
// largas de mundos exportados. El buffer real que reserva es algo mayor
// (1046/1048 WCHAR); el margen sobrante absorbe la extension que se anade a la
// sugerencia sin recalcular limites.
inline constexpr int kMaxPathChars = 1040;

// Filtro de los dos dialogos. Cadena de pares terminada en doble NUL, en el
// formato que espera OPENFILENAMEW.
inline constexpr wchar_t kWorldFilter[] =
    L"Minecraft World (*.mcworld)\0"
    L"*.mcworld\0"
    L"All files (*.*)\0"
    L"*.*\0"
    L"\0";

inline constexpr wchar_t kWorldExtension[] = L".mcworld";
inline constexpr wchar_t kWorldExtensionNoDot[] = L"mcworld";

// Ruta elegida en el ultimo dialogo, o cadena vacia si se cancelo.
// Corresponde al buffer global String1 (0x62997C00).
const wchar_t* LastPickedPath() noexcept;

// True si el ultimo dialogo se confirmo (dword_62998E88).
bool LastPickSucceeded() noexcept;

}  // namespace shim::storage
