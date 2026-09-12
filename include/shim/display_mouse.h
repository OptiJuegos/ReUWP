// DisplayInformation y MouseDevice falsos, respaldados por el HWND.
//
// Original: singletons dword_629979E4 (display, vtable off_629936B0) y
// dword_62998EC0 (raton, vtable off_62993D2C).

#pragma once

#include "shim/winrt_object.h"

namespace shim::winrt {

// Etiquetas de tipo (campo `kind`), tomadas del original.
inline constexpr int kDisplayInformationFactoryKind = 41;
inline constexpr int kDisplayInformationKind = 42;
inline constexpr int kMouseDeviceFactoryKind = 37;
inline constexpr int kMouseDeviceKind = 38;

// DPI que reporta el display falso.
//
// Siempre 96, o sea escala 1.0. El shim renderiza a pixeles fisicos del HWND y
// hace su propio escalado, asi que anunciar el DPI real del sistema haria que el
// juego escalara dos veces.
inline constexpr float kLogicalDpi = 96.0f;

Object* DisplayInformationSingleton() noexcept;
Object* MouseDeviceSingleton() noexcept;

// DisplayInformation::GetForCurrentView (sub_6294CB50)
HRESULT SHIM_COM GetDisplayInformationForCurrentView(Object* factory,
                                                     void** out) noexcept;

// MouseDevice::GetForCurrentView (sub_6294EC90)
HRESULT SHIM_COM GetMouseDeviceForCurrentView(Object* factory,
                                              void** out) noexcept;

}  // namespace shim::winrt
