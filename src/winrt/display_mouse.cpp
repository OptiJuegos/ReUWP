#include "shim/display_mouse.h"

#include "shim/log.h"
#include "shim/winrt_vtable.h"

namespace shim::winrt {
namespace {

// ---------------------------------------------------------------------------
// DisplayInformation
//
// Nota sobre los nombres: el original llama a estos metodos "metric slot 12" y
// "metric slot 13" en sus propias trazas. Eso significa que su autor tampoco
// sabia que metodos eran; los identifico por el indice al que llamaba el juego
// y los rellenó. Se conserva esa nomenclatura en vez de inventar un nombre de
// IDisplayInformation que podria ser el equivocado.
//
// Los indices 6 a 11 son NULL en el original y aqui llevan el stub que traza.
// ---------------------------------------------------------------------------

// La vtable del binario (0x629936B0) tiene 14 entradas: seis de IInspectable,
// seis huecos nulos y los dos metricos al final.
constexpr size_t kDisplayMetricSlot12 = 12;
constexpr size_t kDisplayMetricSlot13 = 13;
constexpr size_t kDisplayVtableSize = kDisplayMetricSlot13 + 1;

// Los dos metricos trazan solo sus primeras cuatro llamadas.
//
// El juego consulta el DPI en cada fotograma; sin el tope, con la traza
// encendida la salida de depuracion no dejaria ver nada mas. El original lleva
// un contador por cada uno, no compartido, y aqui igual.
constexpr LONG kMetricTraceLimit = 4;
LONG g_metric12_calls = 0;  // dword_62998EFC
LONG g_metric13_calls = 0;  // dword_62998F00

// sub_6294CBC0
HRESULT SHIM_COM Display_MetricSlot12(Object* self, float* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = kLogicalDpi;
  if (::InterlockedIncrement(&g_metric12_calls) <= kMetricTraceLimit) {
    log::Write("DisplayInformation metric slot 12 returned 96.0");
  }
  return S_OK;
}

// sub_6294CC30
HRESULT SHIM_COM Display_MetricSlot13(Object* self, float* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = kLogicalDpi;
  if (::InterlockedIncrement(&g_metric13_calls) <= kMetricTraceLimit) {
    log::Write("DisplayInformation metric slot 13 returned 96.0");
  }
  return S_OK;
}

// ---------------------------------------------------------------------------
// MouseDevice
//
// El juego se suscribe a MouseMoved para el raton relativo. El shim acepta la
// suscripcion y devuelve un token cero, pero NUNCA dispara el evento: el
// movimiento entra por los mensajes WM_INPUT / WM_MOUSEMOVE del HWND, que es un
// camino mas fiable y sin la latencia del bombeo de eventos de WinRT.
// ---------------------------------------------------------------------------

// IMouseDevice::add_MouseMoved (sub_6294ED00)
//
// El token es un EventRegistrationToken: un entero de 64 bits, de ahi los dos
// dwords de salida.
HRESULT SHIM_COM Mouse_add_MouseMoved(Object* self, void* handler,
                                      uint32_t* token) noexcept {
  (void)self;
  (void)handler;
  if (token == nullptr) {
    return E_POINTER;
  }
  token[0] = 0;
  token[1] = 0;
  return S_OK;
}

// IMouseDevice::remove_MouseMoved (sub_6294ED30)
//
// El token llega por valor (64 bits = dos parametros de pila). No hay nada que
// dar de baja porque nunca se registro nada.
HRESULT SHIM_COM Mouse_remove_MouseMoved(Object* self, uint32_t token_low,
                                         uint32_t token_high) noexcept {
  (void)self;
  (void)token_low;
  (void)token_high;
  return S_OK;
}

// ---------------------------------------------------------------------------
// Vtables
// ---------------------------------------------------------------------------

Method g_display_vtable[kDisplayVtableSize] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    // 6..11: NULL en el original. Ver la divergencia 3 en RECONSTRUCTION.md.
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    reinterpret_cast<Method>(&Display_MetricSlot12),
    reinterpret_cast<Method>(&Display_MetricSlot13),
};

Method g_mouse_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&Mouse_add_MouseMoved),
    reinterpret_cast<Method>(&Mouse_remove_MouseMoved),
};

Object g_display = {g_display_vtable, 1, kDisplayInformationKind};
Object g_mouse = {g_mouse_vtable, 1, kMouseDeviceKind};

}  // namespace

Object* DisplayInformationSingleton() noexcept {
  return &g_display;
}

Object* MouseDeviceSingleton() noexcept {
  return &g_mouse;
}

HRESULT SHIM_COM GetDisplayInformationForCurrentView(Object* factory,
                                                     void** out) noexcept {
  (void)factory;
  const HRESULT result = ReturnSingleton(DisplayInformationSingleton(), out);
  if (SUCCEEDED(result)) {
    log::Write("DisplayInformation.GetForCurrentView redirected to HWND display");
  }
  return result;
}

HRESULT SHIM_COM GetMouseDeviceForCurrentView(Object* factory,
                                              void** out) noexcept {
  (void)factory;
  const HRESULT result = ReturnSingleton(MouseDeviceSingleton(), out);
  if (SUCCEEDED(result)) {
    log::Write("MouseDevice.GetForCurrentView redirected to HWND mouse");
  }
  return result;
}

}  // namespace shim::winrt
