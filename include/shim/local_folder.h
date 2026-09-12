// Creates the game data tree under the configured save root.
//
// Original: el bloque de SHGetFolderPathW/CreateDirectoryW dentro de
// Win32Bootstrap, justo despues de RoInitialize.
//
// En UWP el sistema le da a la app un LocalFolder ya creado y con permisos. En
// Win32 does not provide that folder, so the shim creates one named after
// SHIM_DISPLAY_NAME before the game requests its first file.
//
// El arbol completo se crea de una vez, aunque el juego solo vaya a escribir en
// la hoja: las versiones antiguas no crean directorios intermedios y fallan en
// silencio si el padre no existe.

#pragma once

#include "shim/common.h"

namespace shim::local_folder {

// Monta el arbol y deja el LocalFolder listo para el modulo de almacenamiento.
//
// Returns false if the configured save root cannot be resolved or is too long. El arranque continua igualmente: el juego fallara mas adelante al
// escribir, pero con un mensaje suyo, que es mas util que abortar aqui.
bool Prepare() noexcept;

// Variante exacta del bootstrap 0.13.2. Solo cambia el seed inicial de
// options.txt: old_game_version=0.13.2 y 0x382 bytes, como el DLL original.
bool Prepare0132() noexcept;

// Carpeta que contiene al ejecutable, o sea Package.InstalledLocation.
//
// De aqui cuelga el arbol de assets del juego. Se deriva del modulo y no del
// directorio de trabajo: son lo mismo cuando se lanza desde la carpeta del
// juego, pero desde cualquier otro sitio el CWD apunta donde no hay nada y el
// juego muere desreferenciando el nulo. Ver
// tools/verify_installed_location_is_module_dir.py.
const wchar_t* InstallDirectory() noexcept;

}  // namespace shim::local_folder
