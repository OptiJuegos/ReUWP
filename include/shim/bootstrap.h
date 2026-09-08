// Arranque del shim: lo que convierte un proceso Win32 corriente en algo que un
// juego UWP acepta como su entorno.
//
// Original: sub_62947150, exportada como Win32Bootstrap (ordinales 56 y 57).
//
// El lanzador carga el shim y llama a esta funcion, que NO retorna hasta que el
// juego se cierra: dentro esta el bucle de mensajes. Es decir, Win32Bootstrap
// hace de main().
//
// El orden de las fases no es negociable. Cada una deja preparado lo que la
// siguiente da por hecho, y varias de ellas tienen que ocurrir antes de que el
// juego ejecute su primera instruccion de inicializacion.

#pragma once

#include "shim/common.h"
#include "shim/game_layout.h"

namespace shim::bootstrap {

// Ejecuta el arranque completo y el bucle de mensajes.
//
// Devuelve el wParam del WM_QUIT si el juego llego a arrancar, o un
// BootstrapResult distinto de kOk si se quedo por el camino.
WPARAM Run() noexcept;

// Bucle de mensajes y fotogramas.
//
// Un bucle de juego, no uno de aplicacion: PeekMessage en vez de GetMessage,
// porque cuando no hay mensajes hay que dibujar, no esperar.
WPARAM RunMessageLoop(void (*run_frame)() noexcept) noexcept;

}  // namespace shim::bootstrap
