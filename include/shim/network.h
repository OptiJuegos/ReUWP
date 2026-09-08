// NetworkInformation falso.
//
// Original: sub_6294B240 (resolucion) y sub_6294B380 (formateo).
//
// El juego usa NetworkInformation para anunciarse en la LAN. Solo necesita una
// direccion IPv4 local presentable, asi que el shim la saca por Winsock y la
// sirve como HostName.

#pragma once

#include "shim/common.h"

namespace shim::net {

// Direccion de reserva si Winsock no da nada util.
//
// No es un valor arbitrario: con loopback el juego arranca y funciona en local,
// que es preferible a fallar. Solo se pierde el multijugador en LAN.
inline constexpr uint8_t kFallbackAddress[4] = {127, 0, 0, 1};

// Longitud maxima de "255.255.255.255" mas el terminador.
inline constexpr size_t kAddressTextCapacity = 16;

// Resuelve la direccion IPv4 local. Idempotente: solo lo hace una vez.
//
// Prefiere la primera direccion que NO sea loopback ni 0.0.0.0. Si todas lo son,
// se queda con la primera que haya visto; si no hay ninguna, con el respaldo.
void ResolveLocalAddress() noexcept;

// Direccion local en texto ("a.b.c.d"), lista para envolver en un HSTRING.
// Fuerza la resolucion si aun no se hizo.
const wchar_t* LocalAddressText() noexcept;

}  // namespace shim::net
