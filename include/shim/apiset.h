// Acceso a las funciones de compatibilidad de api-set desde el resto del shim.
//
// Estas funciones no se pueden declarar aqui con su firma real. El fichero que
// las define compila con el target puesto en Windows 7 para poder llamarse
// CreateFile2 sin chocar con la declaracion del SDK; desde cualquier otra
// unidad de traduccion, el SDK ya la ha declarado y redeclararla es un error, y
// tomarle la direccion haria que el enlazador trajera la de kernel32.
//
// De ahi este rodeo: se expone la direccion como void*, que es lo unico que
// necesita el arranque para meterla en la IAT del juego.

#pragma once

#include "shim/common.h"

namespace shim::apiset {

// Direccion de la CreateFile2 del shim, la que sabe funcionar en Windows 7.
void* CreateFile2Address() noexcept;

}  // namespace shim::apiset
