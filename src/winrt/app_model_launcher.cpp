#include "app_model_internal.h"
#include "shim/winrt_string_compat.h"

#include "shim/log.h"
#include "shim/store.h"

#include <shellapi.h>
#include <winstring.h>

namespace shim::winrt {
namespace {

// LauncherOptions, raw 0x6294DF20/0x6294DF40. Las doce propiedades son pares
// BOOLEAN getter/setter identicos: todos los getters dan false y los setters se
// aceptan sin guardar estado.
HRESULT SHIM_COM LauncherOptionGetFalse(Object* self,
                                        unsigned char* out) noexcept {
  return GenericBoolFalse(self, out);
}

HRESULT SHIM_COM LauncherOptionSetNoOp(Object* self,
                                       unsigned char value) noexcept {
  (void)self;
  (void)value;
  return S_OK;
}

// --- Launcher -------------------------------------------------------------

// Abre una URI con el navegador o la aplicacion asociada del sistema.
//
// El parametro llega como un objeto Uri de WinRT, no como cadena: hay que
// pedirle su texto por su vtable. get_RawUri esta en el indice 6, detras de
// IInspectable.
bool LaunchUriObject(void* uri) noexcept {
  if (uri == nullptr || ::IsBadReadPtr(uri, sizeof(void*))) {
    return false;
  }
  void* const* vtable = *static_cast<void* const* const*>(uri);
  if (vtable == nullptr || ::IsBadReadPtr(vtable, 7 * sizeof(void*))) {
    return false;
  }

  using GetRawUriFn = HRESULT(SHIM_COM*)(void*, HSTRING*);
  const auto get_raw_uri = reinterpret_cast<GetRawUriFn>(vtable[6]);
  if (get_raw_uri == nullptr) {
    return false;
  }

  HSTRING raw = nullptr;
  if (FAILED(get_raw_uri(uri, &raw)) || raw == nullptr) {
    return false;
  }

  UINT32 length = 0;
  const wchar_t* text = shim::winrt_string::GetRawBuffer(raw, &length);
  bool launched = false;
  if (text != nullptr && length != 0) {
    // ShellExecuteW devuelve un "codigo de error" mayor que 32 cuando va bien.
    // Es una API de los tiempos de Win16 y conserva esa rareza.
    launched = reinterpret_cast<INT_PTR>(::ShellExecuteW(
                   nullptr, L"open", text, nullptr, nullptr, SW_SHOWNORMAL)) >
               32;
  }
  shim::winrt_string::Delete(raw);
  return launched;
}

// Launcher::LaunchUriAsync (sub_6294DD70)
HRESULT SHIM_COM LaunchUriAsync(Object* self, void* uri,
                                void** operation) noexcept {
  (void)self;
  if (operation == nullptr) {
    return E_POINTER;
  }
  const bool launched = LaunchUriObject(uri);
  log::Write(launched ? "Windows.System.Launcher opened URI via ShellExecuteW"
                      : "Windows.System.Launcher URL fallback returned "
                        "completed operation");
  // Se devuelve una operacion completada pase lo que pase. Al juego no le sirve
  // de nada saber que no se abrio el navegador, y un fallo aqui le haria
  // mostrar un mensaje de error por algo secundario.
  return ReturnSingleton(store::BooleanOperation(), operation);
}

// LaunchUriAsync con opciones (sub_6294DE60): mismas semanticas, un parametro
// mas que se ignora. Su propia funcion porque la aridad cambia.
HRESULT SHIM_COM LaunchUriWithOptionsAsync(Object* self, void* uri,
                                           void* options,
                                           void** operation) noexcept {
  (void)options;
  return LaunchUriAsync(self, uri, operation);
}

// --- CachedFileManager ----------------------------------------------------

// DeferUpdates (sub_6294E930): sin efecto.
//
// En UWP esto pospone la sincronizacion de un fichero compartido. Aqui los
// ficheros son ficheros del disco y no hay nada que posponer.
HRESULT SHIM_COM DeferUpdates(Object* self, void* file) noexcept {
  (void)self;
  (void)file;
  return S_OK;
}

// CompleteUpdatesAsync (sub_6294E940)
HRESULT SHIM_COM CompleteUpdatesAsync(Object* self, void* file,
                                      void** operation) noexcept {
  (void)self;
  (void)file;
  if (operation == nullptr) {
    return E_POINTER;
  }
  return ReturnSingleton(store::NullResultOperation(), operation);
}


#define SHIM_IINSPECTABLE                              \
  reinterpret_cast<Method>(&QueryInterface),           \
      reinterpret_cast<Method>(&AddRef),               \
      reinterpret_cast<Method>(&Release),              \
      reinterpret_cast<Method>(&GetIids),              \
      reinterpret_cast<Method>(&GetRuntimeClassName),  \
      reinterpret_cast<Method>(&GetTrustLevel)

// ILauncherOptions (kind 69, raw vtable 0x62993A4C): 30 slots. Despues de
// IInspectable hay doce pares getter/setter, todos 0x6294DF20/0x6294DF40.
Method g_launcher_options_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&LauncherOptionGetFalse), reinterpret_cast<Method>(&LauncherOptionSetNoOp),
    reinterpret_cast<Method>(&LauncherOptionGetFalse), reinterpret_cast<Method>(&LauncherOptionSetNoOp),
    reinterpret_cast<Method>(&LauncherOptionGetFalse), reinterpret_cast<Method>(&LauncherOptionSetNoOp),
    reinterpret_cast<Method>(&LauncherOptionGetFalse), reinterpret_cast<Method>(&LauncherOptionSetNoOp),
    reinterpret_cast<Method>(&LauncherOptionGetFalse), reinterpret_cast<Method>(&LauncherOptionSetNoOp),
    reinterpret_cast<Method>(&LauncherOptionGetFalse), reinterpret_cast<Method>(&LauncherOptionSetNoOp),
    reinterpret_cast<Method>(&LauncherOptionGetFalse), reinterpret_cast<Method>(&LauncherOptionSetNoOp),
    reinterpret_cast<Method>(&LauncherOptionGetFalse), reinterpret_cast<Method>(&LauncherOptionSetNoOp),
    reinterpret_cast<Method>(&LauncherOptionGetFalse), reinterpret_cast<Method>(&LauncherOptionSetNoOp),
    reinterpret_cast<Method>(&LauncherOptionGetFalse), reinterpret_cast<Method>(&LauncherOptionSetNoOp),
    reinterpret_cast<Method>(&LauncherOptionGetFalse), reinterpret_cast<Method>(&LauncherOptionSetNoOp),
    reinterpret_cast<Method>(&LauncherOptionGetFalse), reinterpret_cast<Method>(&LauncherOptionSetNoOp),
};
static_assert(CountOf(g_launcher_options_vtable) == 30,
              "LauncherOptions must match original 30-slot vtable");

// ILauncherStatics (0x62993A24): las dos primeras entradas son LaunchFileAsync,
// que el juego no usa; las URI estan en el 8 y el 9.
Method g_launcher_vtable[] = {
    SHIM_IINSPECTABLE,
    nullptr,
    nullptr,
    reinterpret_cast<Method>(&LaunchUriAsync),
    reinterpret_cast<Method>(&LaunchUriWithOptionsAsync),
};

// ICachedFileManagerStatics (0x62993C58).
Method g_cached_file_manager_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&DeferUpdates),
    reinterpret_cast<Method>(&CompleteUpdatesAsync),
};

Object g_launcher_options_factory = {g_activation_factory_vtable, 1,
                                     kLauncherOptionsFactory};
Object g_launcher_options = {g_launcher_options_vtable, 1, kLauncherOptions};
Object g_launcher = {g_launcher_vtable, 1, kLauncher};
Object g_cached_file_manager = {g_cached_file_manager_vtable, 1,
                                kCachedFileManager};

}  // namespace

Object* LauncherOptionsFactory() noexcept { return &g_launcher_options_factory; }
Object* LauncherStatics() noexcept { return &g_launcher; }
Object* CachedFileManagerStatics() noexcept { return &g_cached_file_manager; }

namespace app_model_detail {

Object* LauncherOptionsInstance() noexcept { return &g_launcher_options; }

}  // namespace app_model_detail

#undef SHIM_IINSPECTABLE

}  // namespace shim::winrt
