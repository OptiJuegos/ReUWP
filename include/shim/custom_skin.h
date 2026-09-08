// Selector de skin personalizado de Minecraft 0.13.2.
//
// El juego UWP original abre un selector WinRT. d3dcraft 0.13.2 reemplaza esa
// ruta por GetOpenFileNameW, valida que el fichero elegido sea un PNG y lo
// instala como custom.png dentro del LocalFolder antes de actualizar options.txt.

#pragma once

#include "shim/common.h"

namespace shim::custom_skin {

// Muestra el selector modal y, si el usuario elige un PNG valido, lo instala en
// las dos rutas que usa 0.13.2 y marca Standard_Custom en options.txt.
//
// Devuelve true solo cuando al menos una copia del PNG y el parche de opciones
// han terminado correctamente. Cancelar el dialogo devuelve false y no es un
// error fatal para el juego.
bool ShowAndInstall() noexcept;

}  // namespace shim::custom_skin
