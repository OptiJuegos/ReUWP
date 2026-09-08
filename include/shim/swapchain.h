// Cadena de intercambio sobre HWND, e inyeccion en el renderer del juego.
//
// Original: el bloque central de sub_62947FF0 (745 lineas), mas el camino de
// presentacion de reserva que vive dentro del bucle de mensajes.
//
//
// EL PROBLEMA QUE RESUELVE
//
// Una app UWP no crea su cadena de intercambio: llama a
// IDXGIFactory2::CreateSwapChainForCoreWindow y el sistema la asocia al
// CoreWindow. Aqui no hay CoreWindow, y en Windows 7 tampoco hay IDXGIFactory2.
//
// La solucion del shim es dejar que el juego construya su DX::DeviceResources
// como quiera, y despues **sustituirle la cadena de intercambio por una creada
// sobre el HWND**. El juego sigue dibujando en sus propias vistas sin enterarse
// de que lo que hay debajo es una ventana de escritorio.
//
// Eso implica escribir dentro de un objeto del juego, en un desplazamiento que
// cambia entre versiones. Es la parte mas fragil del shim entero.

#pragma once

#include "shim/common.h"

namespace shim::game {
struct D3DLayout;
}

namespace shim::d3d {

void BindLayout(const game::D3DLayout* layout) noexcept;

// Formato preferido y de reserva de la cadena.
//
// Se pide BGRA porque es lo que quiere el compositor de escritorio y evita una
// conversion por fotograma. Si falla en Windows 7 se reintenta con RGBA, que en
// controladores viejos a veces es el unico soportado para presentar.
inline constexpr DWORD kFormatBgra = 87;  // DXGI_FORMAT_B8G8R8A8_UNORM
inline constexpr DWORD kFormatRgba = 28;  // DXGI_FORMAT_R8G8B8A8_UNORM

// Crea la cadena de intercambio sobre la ventana, a partir del dispositivo que
// el juego ya creo.
//
// Recorre ID3D11Device -> IDXGIDevice -> IDXGIAdapter -> IDXGIFactory, que es la
// unica forma de llegar a la fabrica que fabrico un dispositivo concreto. Usar
// CreateDXGIFactory daria otra fabrica y la cadena no se podria asociar.
bool CreateForWindow(void* device, HWND window) noexcept;

// Mete la cadena del shim dentro del DX::DeviceResources del juego.
//
// Suelta la que hubiera antes y toma referencia de la nueva. Es lo que hace que
// el juego presente en la ventana en vez de en su cadena de UWP.
bool InjectIntoRenderer(void* renderer) noexcept;

// Cadena y vista de render actuales. Nulas antes de CreateForWindow.
void* SwapChain() noexcept;
void* RenderTargetView() noexcept;

// Guarda el contexto inmediato, que hace falta para el camino de reserva.
void SetDeviceContext(void* context) noexcept;

// Toma la referencia COM adicional que conserva el host original una vez que
// swap chain + RTV ya quedaron validos. SetDeviceContext mantiene inicialmente
// un puntero prestado para que una ruta de error temprana no deje una referencia
// extra viva; este helper se llama solo al completar el bring-up grafico.
bool RetainDeviceContext() noexcept;

// Instala el reemplazo de IDXGISwapChain::Present una vez que el renderer ya
// tiene RTV/contexto validos. El original lo hace tarde, no durante
// CreateSwapChain, para que una ruta de error de targets no deje la vtable
// parcheada a medias.
bool InstallGamePresentHook() noexcept;

// Instala los hooks de ID3D11DeviceContext que el host Win32 moderno necesita.
// En 0.15.10/1.1.5 el original sustituye RSSetScissorRects (vtable +0xB4)
// para recortar rectangulos UWP al area cliente real del HWND.
bool InstallModernContextHooks(HWND window) noexcept;

// Contexto inmediato guardado. Nulo antes de SetDeviceContext.
void* DeviceContext() noexcept;

// Presents the current game backbuffer. `allow_tearing` selects SyncInterval 0;
// the normal platform path uses synchronized presentation on every supported OS.
HRESULT Present(bool allow_tearing) noexcept;

// Presents using the platform backend default pacing.
HRESULT PresentPlatformDefault() noexcept;

// Generacion de Present aceptados por el 0.13.2 hook. El bucle de
// esa version la consulta antes y despues de AppMain::frame para saber si el
// juego ya presento por su cuenta.
unsigned long PresentGeneration() noexcept;

// Ruta de carga exacta de Minecraft 0.13.2. El original conserva rojo/verde
// fijos y hace pulsar solo el canal azul entre 0.12 y ~0.18, reiniciando la
// fase al superar 0.06. Se mantiene separada del fallback moderno para no
// cambiar el comportamiento de 0.15.10/1.1.5.
void PresentV0132LoadingFrame() noexcept;

// Minecraft 0.15.10 loading path. It keeps red/green fixed, pulses the blue
// channel and presents with SyncInterval=1.
void Present01510LoadingFrame() noexcept;

// Prepara el contexto para que el juego dibuje: viewport del tamano de la
// ventana y la vista de render enlazada.
//
// Hay que hacerlo en cada fotograma, no una vez. El juego cambia el viewport
// para sus pasadas de sombras y no lo restaura, asi que si no se repone aqui el
// segundo fotograma dibuja al tamano de un mapa de sombras.
void BeginFrame(int width, int height, void* depth_target = nullptr) noexcept;

// Prepara un resize soltando todas las referencias del shim al backbuffer.
// 0.13.2 necesita esta fase separada porque entre el unbind/Flush y
// ResizeBuffers llama a su propio destructor de render targets.
void PrepareForResize() noexcept;

// Ruta generica: compone las dos fases anteriores.
//
// Se llama al soltar el borde, no durante el arrastre: ver la nota de
// app_window::OnResize.
bool Resize(int width, int height) noexcept;


}  // namespace shim::d3d
