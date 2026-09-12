// CoreWindow y CoreDispatcher falsos, respaldados por un HWND de Win32.
//
// Original: singletons dword_62998E9C (ventana) y dword_62998EA8 (dispatcher),
// con vtables en off_62993CC0 y off_62993CE8.
//
// El juego UWP pide la ventana con CoreWindow::GetForCurrentThread y encola
// trabajo en su CoreDispatcher. Aqui la ventana es un HWND normal y el
// dispatcher una cola propia que se drena desde el bucle de mensajes.

#pragma once

#include "shim/winrt_object.h"

namespace shim::winrt {

// Etiquetas de tipo de los dos singletons (el campo `kind`, en +8). Los valores
// son los del original.
inline constexpr int kCoreWindowKind = 38;
inline constexpr int kCoreDispatcherKind = 38;

// Profundidad maxima de la cola del dispatcher.
//
// Al llenarse se rechazan los manejadores nuevos en vez de crecer sin limite:
// si el juego encola mas rapido de lo que se drena, prefiere perder trabajo
// antes que agotar la memoria.
inline constexpr unsigned kDispatcherQueueCapacity = 64;

// Singletons. Nunca se destruyen; ver la nota de inmortalidad en
// winrt_object.h.
Object* CoreWindowSingleton() noexcept;
Object* CoreDispatcherSingleton() noexcept;

// Deja los dos objetos listos para usarse. La llama el bootstrap tras crear el
// HWND.
void Initialize() noexcept;

// Drains at most `max_handlers` entries from the dispatcher queue. Remaining
// callbacks stay queued for the next frame.
void ProcessQueuedHandlers(unsigned max_handlers) noexcept;

// ---------------------------------------------------------------------------
// Metodos de factoria estatica
//
// Los invoca el juego a traves de la factoria de activacion. Devuelven el
// singleton correspondiente con el contador incrementado.
// ---------------------------------------------------------------------------

// CoreWindow::GetForCurrentThread (sub_6294E9F0)
HRESULT SHIM_COM GetCoreWindowForCurrentThread(Object* factory,
                                               void** out) noexcept;

}  // namespace shim::winrt
