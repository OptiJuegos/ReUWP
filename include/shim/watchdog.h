// Vigilante del hilo principal.
//
// Original: sub_62955250 y el hilo que lo arma.
//
// El constructor del juego puede quedarse colgado esperando algo que en Win32
// no llega nunca (una API de WinRT, un servicio de Xbox Live). Cuando eso pasa
// no hay excepcion ni fallo: el proceso simplemente se queda quieto, y sin esto
// no habria forma de saber donde.
//
// El vigilante arma dos disparos, a 3 y 10 segundos. Al saltar suspende el hilo
// principal, le lee el contexto, vuelca EIP/ESP/EBP y la cadena de llamadas, y
// lo reanuda. Es puramente observacional.

#pragma once

#include "shim/common.h"

namespace shim::diag {

// Momentos en que se toman las fotos, en milisegundos desde que se arma.
inline constexpr DWORD kFirstSnapshotDelayMs = 3000;
inline constexpr DWORD kSecondSnapshotDelayMs = 10000;

// Registra el hilo a vigilar. Sin esto el vigilante no hace nada.
void SetWatchedThread(DWORD thread_id) noexcept;

// Marca si el constructor AppMainXaml de 1.1.5 sigue dentro de la factory.
// El hilo watchdog solo toma snapshots mientras esta bandera permanezca activa.
void SetConstructorActive(bool active) noexcept;

// Lanza el hilo vigilante con los dos disparos armados.
//
// Devuelve false si no se pudo crear el hilo o si no hay hilo registrado.
bool ArmConstructorWatchdog() noexcept;

// Toma una foto del hilo vigilado ahora mismo.
//
// `label` describe el motivo y aparece en la traza; nulo se rinde como
// "snapshot".
//
// Suspender el hilo principal desde otro hilo es intrinsecamente arriesgado: si
// lo pilla dentro del cargador o del heap, cualquier cosa que haga este codigo
// mientras tanto puede interbloquear. Por eso entre suspender y reanudar solo
// se lee contexto y se recorre pila, sin reservar memoria ni tomar cerrojos.
void CaptureThreadSnapshot(const char* label) noexcept;

}  // namespace shim::diag
