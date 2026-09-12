// La ventana Win32 que sustituye al CoreWindow de UWP.
//
// Original: el bloque de RegisterClassExW/CreateWindowExW dentro de
// Win32Bootstrap y el procedimiento de ventana sub_62947A30.
//
// Una app UWP no crea ventanas: recibe un CoreWindow ya hecho y se suscribe a
// sus eventos. El shim invierte eso. Crea una ventana de escritorio corriente,
// y el CoreWindow falso que el juego cree tener no es mas que una fachada sobre
// este HWND. De ahi que el procedimiento de ventana sea el punto por el que
// entra toda la entrada del usuario al juego.

#pragma once

#include "shim/common.h"

namespace shim::app_window {

// Dimensiones iniciales. El original las lleva en una constante de 16 bytes
// cargada de golpe (dwStyle) junto al estilo de ventana.
inline constexpr int kDefaultWidth = 1280;
inline constexpr int kDefaultHeight = 720;

// WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS.
inline constexpr DWORD kWindowStyle = 0x10CF0000;
static_assert((kWindowStyle & WS_THICKFRAME) != 0,
              "ReUWP window style must remain resizable");

// Registra la clase de ventana y crea la ventana principal.
//
// Devuelve nullptr si falla; el llamante distingue el motivo por el ultimo
// error. El nombre de clase y el titulo dependen de la version del juego, que
// debe estar ya detectada.
HWND Create(HINSTANCE module) noexcept;

// Ventana principal (dword_62996030). nullptr antes de Create.
HWND Handle() noexcept;

// Requests the normal shutdown path for the main window. If the window does
// not exist yet, terminates the current message loop with WM_QUIT.
bool RequestClose() noexcept;

// Procedimiento de ventana (sub_62947A30).
//
// Publico porque el arranque lo instala en la WNDCLASSEXW y porque tenerlo a la
// vista deja claro que es la unica puerta de entrada de eventos del sistema.
LRESULT SHIM_COM WindowProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) noexcept;

// Handler opcional para versiones cuyo resize no se limita a DXGI.
// Devuelve true cuando la ruta completa de la version se pudo aplicar.
using ResizeHandler = bool (*)(int width, int height) noexcept;
void SetResizeHandler(ResizeHandler handler) noexcept;

// Ultimo tamano de area cliente conocido, en pixeles.
//
// Lo consulta el arranque para dar al juego un tamano inicial antes del primer
// fotograma, cuando todavia no ha llegado ningun WM_SIZE.
void ClientSize(int* width, int* height) noexcept;

// Notifica un cambio de tamano al motor grafico.
//
// Durante un arrastre de borde el sistema manda un WM_SIZE por pixel movido.
// Recrear la cadena de intercambio en cada uno tira el rendimiento y en
// Windows 7 llega a fallar, asi que el cambio se aplaza hasta soltar el raton.
void OnResize(int width, int height) noexcept;

// Cambia entre ventana normal y borderless fullscreen conservando exactamente
// la colocacion y estilos Win32 previos. Es la reconstruccion de 0x6295C620.
bool SetBorderlessFullscreen(bool enabled) noexcept;

}  // namespace shim::app_window
