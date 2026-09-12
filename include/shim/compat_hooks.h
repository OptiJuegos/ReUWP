// Sustitutos de compatibilidad que el arranque instala en la IAT del juego.
//
// Original (raw disassembly): 0x6294EEF0/EF00/EF90/EFC0 (vccorlib),
// 0x6294EFE0 (D3DCompile) y 0x6294F340 (D3D11CreateDevice).
//
// Los tres arreglan cosas distintas, pero comparten forma: se sustituye un hueco
// de la tabla de importacion, se hace algo antes o despues, y se encadena a la
// funcion original.

#pragma once

#include "shim/common.h"

namespace shim::game {
struct PatchLayout;
}

namespace shim::hooks {

// Bandera que se fuerza siempre en D3D11CreateDevice.
//
// D3D11_CREATE_DEVICE_BGRA_SUPPORT. Sin ella no se puede crear una cadena de
// intercambio en BGRA, que es el formato que quiere el compositor de escritorio.
inline constexpr UINT kCreateDeviceBgraSupport = 0x20;

// Nivel de caracteristicas que se filtra en Windows 7.
//
// D3D_FEATURE_LEVEL_11_1. No existe en Windows 7 sin la actualizacion de
// plataforma, y pedirlo hace fallar la creacion entera del dispositivo aunque el
// resto de niveles de la lista fueran perfectamente validos.
inline constexpr UINT kFeatureLevel11_1 = 0xB100;

// Tipos de controlador de D3D11.
inline constexpr UINT kDriverTypeHardware = 1;
inline constexpr UINT kDriverTypeWarp = 5;

// Instala los tres sustitutos sobre la IAT de la version detectada.
//
// Cada uno se salta solo si su hueco no esta en la tabla de la version actual.
// Ninguno es imprescindible para arrancar, pero sin ellos el juego falla mas
// adelante y de formas peores de diagnosticar.
void InstallCompatibility(const game::PatchLayout& layout) noexcept;

// Consume el par ID3D11Device/ID3D11DeviceContext retenido por el hook de
// D3D11CreateDevice. En Windows 7 rellena los campos que el inicializador del
// juego haya dejado nulos y transfiere la referencia; siempre libera cualquier
// referencia sobrante al terminar.
void FinalizeRetainedD3DDeviceFallback(void* owner, DWORD device_offset,
                                       void* renderer,
                                       DWORD context_offset) noexcept;

// Instala solo la parte D3D/Win7 que usa Minecraft 0.13.2.
//
// Esa rama no comparte la tabla Layout de 0.15.10/1.1.5 y tampoco instala el
// bloque vccorlib de esas versiones. Sus tres huecos D3D y dos GUID viven en
// Version0132Layout, asi que se mantienen en una entrada separada para no fingir
// que ambas ABI son intercambiables.
void InstallV0132Compatibility() noexcept;

}  // namespace shim::hooks
