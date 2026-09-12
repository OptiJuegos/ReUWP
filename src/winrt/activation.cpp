#include "shim/activation.h"
#include "shim/winrt_string_compat.h"

#include <winstring.h>

#include "shim/app_model.h"
#include "shim/core_window.h"
#include "shim/crypto_buffer.h"
#include "shim/display_mouse.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/memory.h"
#include "shim/platform_info.h"
#include "shim/storage.h"
#include "shim/store.h"
#include "shim/system_services.h"
#include "shim/winrt_object.h"
#include "shim/winrt_vtable.h"

namespace shim::winrt {
namespace {

ActivationFactoryFn g_fallback = nullptr;  // dword_62998ED8

// `kind` es parte de la identidad interna del objeto, no una etiqueta
// generica de "factory". Los valores salen de la tabla de singletons original.
constexpr int kCoreWindowFactoryKind = 37;
constexpr int kAnalyticsFactoryKind = 65;

constexpr GUID kIidIActivationFactory = {
    0x00000035, 0x0000, 0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

using ReleaseFn = ULONG(SHIM_COM*)(void* self);
using ActivateInstanceFn = HRESULT(SHIM_COM*)(void* self, void** instance);

// --- Fabricas -------------------------------------------------------------
//
// Una fabrica no es el objeto que el juego quiere: es el objeto del que lo
// saca. `Windows.UI.Core.CoreWindow` devuelve una fabrica con un unico metodo,
// GetForCurrentThread, y es esa llamada la que entrega el CoreWindow.

Method g_core_window_factory_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&GetCoreWindowForCurrentThread),
};

Method g_mouse_factory_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&GetMouseDeviceForCurrentView),
};

Method g_display_factory_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&GetDisplayInformationForCurrentView),
};

Method g_analytics_factory_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&GetAnalyticsVersionInfo),
};

HRESULT SHIM_COM GetCurrentAppNull(Object* self, void** out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  return S_OK;
}

HRESULT SHIM_COM GetCurrentAppId(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  // Raw 0.15.10 disassembly 0x6294AAA0 creates this exact 15-char ID.
  return shim::winrt_string::Create(L"Minecraft.Win32", 15, out);
}

// CurrentApp se devuelve directo, sin fabrica intermedia: sus miembros son
// estaticos, asi que el objeto de clase y la fabrica son la misma cosa.
Method g_current_app_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    reinterpret_cast<Method>(&store::GetLicenseInformation),
    reinterpret_cast<Method>(&GetCurrentAppNull),
    reinterpret_cast<Method>(&GetCurrentAppId),
    reinterpret_cast<Method>(&store::RequestReceiptAsync),
    reinterpret_cast<Method>(&store::RequestReceiptAsync),
    reinterpret_cast<Method>(&store::LoadListingInformationAsync),
    reinterpret_cast<Method>(&store::RequestReceiptAsync),
    reinterpret_cast<Method>(&store::RequestReceiptAsync),
};
static_assert(CountOf(g_current_app_vtable) == 14,
              "CurrentApp vtable must match original 14-slot layout");

// ICurrentAppWithConsumables: interfaz aparte, con los tres primeros huecos
// nulos en el original y el consumible en el indice 9.
Method g_current_app_consumables_vtable[] = {
    reinterpret_cast<Method>(&QueryInterface),
    reinterpret_cast<Method>(&AddRef),
    reinterpret_cast<Method>(&Release),
    reinterpret_cast<Method>(&GetIids),
    reinterpret_cast<Method>(&GetRuntimeClassName),
    reinterpret_cast<Method>(&GetTrustLevel),
    nullptr,
    nullptr,
    nullptr,
    reinterpret_cast<Method>(&store::GetUnfulfilledConsumablesAsync),
};

Object g_core_window_factory = {g_core_window_factory_vtable, 1,
                                kCoreWindowFactoryKind};
Object g_mouse_factory = {g_mouse_factory_vtable, 1, kMouseDeviceFactoryKind};
Object g_display_factory = {g_display_factory_vtable, 1,
                            kDisplayInformationFactoryKind};
Object g_analytics_factory = {g_analytics_factory_vtable, 1,
                              kAnalyticsFactoryKind};
Object g_current_app = {g_current_app_vtable, 1, 3};
Object g_current_app_consumables = {g_current_app_consumables_vtable, 1, 4};

// --- Tabla de redireccion -------------------------------------------------

// Cada entrada resuelve su objeto por funcion, no por direccion directa: unos
// viven aqui y otros en el modulo de su clase, y un puntero a otra unidad de
// traduccion no vale como inicializador constante.
struct Entry {
  const wchar_t* class_name;
  Object* (*resolve)() noexcept;
};

Object* CoreWindowFactory() noexcept { return &g_core_window_factory; }
Object* MouseFactory() noexcept { return &g_mouse_factory; }
Object* DisplayFactory() noexcept { return &g_display_factory; }
Object* AnalyticsFactory() noexcept { return &g_analytics_factory; }
Object* CurrentAppObject() noexcept { return &g_current_app; }

// Clases que el shim resuelve por si mismo.
//
// El original compara con lstrcmpW en cadena, una detras de otra; aqui es una
// tabla porque el resultado es el mismo y se lee. El orden no importa: los
// nombres son unicos.
const Entry kRedirects[] = {
    {L"Windows.UI.Core.CoreWindow", &CoreWindowFactory},
    {L"Windows.Devices.Input.MouseDevice", &MouseFactory},
    {L"Windows.Graphics.Display.DisplayInformation", &DisplayFactory},
    {L"Windows.System.Profile.AnalyticsInfo", &AnalyticsFactory},
    // ApiInformation no lleva fabrica intermedia: el objeto devuelto ES el que
    // responde a las consultas, porque toda su interfaz es estatica.
    {L"Windows.Foundation.Metadata.ApiInformation", &ApiInformationSingleton},
    {L"Windows.ApplicationModel.Store.CurrentApp", &CurrentAppObject},

    // Modelo de aplicacion: donde esta instalado, donde escribe, cuanta memoria
    // hay y como se llama. Es lo primero que el juego pregunta al arrancar.
    {L"Windows.ApplicationModel.Package", &PackageFactory},
    {L"Windows.Storage.ApplicationData", &ApplicationDataFactory},
    {L"Windows.System.MemoryManager", &MemoryManagerStatics},

    // Idioma y region: fijos en en-US/US.
    {L"Windows.Globalization.ApplicationLanguages",
     &ApplicationLanguagesStatics},
    {L"Windows.Globalization.GeographicRegion", &GeographicRegionStatics},

    // XAML: el juego consulta Application.Current y Window.Current durante el
    // arranque aunque no use XAML para nada mas.
    {L"Windows.UI.Xaml.Application", &XamlApplicationStatics},
    {L"Windows.UI.Xaml.Window", &XamlWindowStatics},

    // Sistema.
    {L"Windows.System.Launcher", &LauncherStatics},
    {L"Windows.System.LauncherOptions", &LauncherOptionsFactory},
    {L"Windows.System.Threading.ThreadPool", &ThreadPoolStatics},

    // Red. ResourceManager/ResourceContext y SpeechSynthesizer son casos
    // versionados y se resuelven antes de esta tabla, tal como sub_62949D90.
    {L"Windows.Networking.Connectivity.NetworkInformation",
     &NetworkInformationStatics},
    {L"Windows.Security.Cryptography.CryptographicBuffer",
     &CryptographicBufferStatics},
};

// Clases que el original tambien redirige pero cuyos objetos todavia no estan
// reconstruidos. Se listan para que la traza diga que falta exactamente, en vez
// de dejarlas caer al sistema en silencio y que el fallo aparezca mas tarde y
// sin contexto.
// Ya no queda ninguna: las 25 clases que el original intercepta estan
// resueltas. Se conserva el mecanismo porque es lo que hace visible en la traza
// cualquier clase que aparezca en una version del juego no contemplada.
const wchar_t* const kPending[] = {
    nullptr,
};

// IID de ICurrentAppWithConsumables, leido del binario en 0x629891AC.
//
// El original lo compara con memcmp de 16 bytes contra esa constante embebida.
// Es la unica clase de la tabla que devuelve un objeto distinto segun la
// interfaz pedida, no solo segun el nombre.
constexpr GUID kCurrentAppWithConsumables = {
    0x844E0071,
    0x9E4F,
    0x4F79,
    {0x99, 0x5A, 0x5F, 0x91, 0x17, 0x2E, 0x6C, 0xEF}};

// 0x629891BC: ISpeechSynthesizerStatics. El original solo consulta este IID
// cuando ejecuta la ruta especial 1.1.5 + Windows 7.
constexpr GUID kSpeechSynthesizerStatics = {
    0x7D526ECC, 0x7533, 0x4C3F,
    {0x85, 0xBE, 0x88, 0x8C, 0x2B, 0xAE, 0xEB, 0xDC}};

bool SameGuid(const GUID* a, const GUID& b) noexcept {
  if (a == nullptr) {
    return false;
  }
  return memcmp(a, &b, sizeof(GUID)) == 0;
}

// Traza de la clase pedida. Se hace en ANSI porque toda la salida de
// diagnostico del shim lo es, y el nombre de una clase WinRT es ASCII puro.
void LogClassName(const char* what, const wchar_t* class_name) noexcept {
  char narrow[256] = {};
  if (class_name != nullptr) {
    ::WideCharToMultiByte(CP_UTF8, 0, class_name, -1, narrow,
                          static_cast<int>(sizeof(narrow)), nullptr, nullptr);
    narrow[sizeof(narrow) - 1] = '\0';
  }
  log::Writef("%s %s", what, narrow[0] != '\0' ? narrow : "(null)");
}

}  // namespace

void SetActivationFallback(ActivationFactoryFn original) noexcept {
  g_fallback = original;
}

void InitializeActivation() noexcept {
  // Los objetos son estaticos y ya estan construidos; esto solo deja constancia
  // en la traza de cuantas clases se van a interceptar de verdad.
  log::Writef("WinRT activation table: %u redirected, %u pending",
              static_cast<unsigned int>(CountOf(kRedirects)),
              static_cast<unsigned int>(CountOf(kPending)));
}

static HRESULT ResolveActivationFactoryByName(const wchar_t* class_name,
                                              const GUID* iid, void** factory,
                                              bool allow_fallback) noexcept {
  if (factory == nullptr) {
    return E_POINTER;
  }
  *factory = nullptr;

  if (class_name != nullptr) {
    // sub_62949D90 gates ResourceContext/ResourceManager only on the 1.1.5
    // build flag. The separate Windows 7 flag is used later for SpeechSynthesizer,
    // not for the resource compatibility objects.
    if (game::HasCapability(game::VersionCapability::kResourceActivation) &&
        ::lstrcmpW(
            class_name,
            L"Windows.ApplicationModel.Resources.Core.ResourceContext") == 0) {
      log::Write("redirected unpackaged 1.1.5 ResourceContext factory");
      return ReturnSingleton(ResourceContextForInterface(iid), factory);
    }
    if (game::HasCapability(game::VersionCapability::kResourceActivation) &&
        ::lstrcmpW(
            class_name,
            L"Windows.ApplicationModel.Resources.Core.ResourceManager") == 0) {
      log::Write("redirected unpackaged 1.1.5 ResourceManager factory");
      return ReturnSingleton(ResourceManagerStatics(), factory);
    }

    // The three storage activation replacements are also 1.1.5-only in
    // sub_62949D90. 0.15.10 must fall through to the platform instead of
    // receiving the HWND-host picker/cache shims.
    if (game::HasCapability(game::VersionCapability::kStoragePickerActivation) &&
        ::lstrcmpW(class_name, L"Windows.Storage.Pickers.FileSavePicker") == 0) {
      log::Write("redirected Windows.Storage.Pickers.FileSavePicker");
      return ReturnSingleton(storage::SavePickerFactory(), factory);
    }
    if (game::HasCapability(game::VersionCapability::kStoragePickerActivation) &&
        ::lstrcmpW(class_name, L"Windows.Storage.Pickers.FileOpenPicker") == 0) {
      log::Write("redirected Windows.Storage.Pickers.FileOpenPicker");
      return ReturnSingleton(storage::OpenPickerFactory(), factory);
    }
    if (game::HasCapability(game::VersionCapability::kStoragePickerActivation) &&
        ::lstrcmpW(class_name, L"Windows.Storage.CachedFileManager") == 0) {
      log::Write("redirected Windows.Storage.CachedFileManager");
      return ReturnSingleton(CachedFileManagerStatics(), factory);
    }

    // Speech is even narrower in the original: only 1.1.5 on Windows 7 needs
    // the local implementation. On newer Windows the request must fall through
    // to the platform. The statics IID selects the AllVoices/DefaultVoice
    // object; every other IID receives the activation factory.
    if (game::HasCapability(game::VersionCapability::kWin7SpeechActivation) && game::IsWindows7() &&
        ::lstrcmpW(
            class_name,
            L"Windows.Media.SpeechSynthesis.SpeechSynthesizer") == 0) {
      if (SameGuid(iid, kSpeechSynthesizerStatics)) {
        log::Write("redirected Win7 SpeechSynthesizer installed voices");
        return ReturnSingleton(SpeechSynthesizerStatics(), factory);
      }
      log::Write("redirected Win7 SpeechSynthesizer activation factory");
      return ReturnSingleton(SpeechSynthesizerFactory(), factory);
    }

    if (::lstrcmpW(
            class_name,
            L"Windows.ApplicationModel.Core.CoreApplication") == 0) {
      log::Write("redirected CoreApplication interface");
      return ReturnSingleton(CoreApplicationForInterface(iid), factory);
    }

    // CurrentApp va antes que la tabla porque es la otra clase que discrimina
    // por IID: el juego pide dos interfaces distintas con el mismo nombre.
    if (::lstrcmpW(class_name,
                   L"Windows.ApplicationModel.Store.CurrentApp") == 0 &&
        SameGuid(iid, kCurrentAppWithConsumables)) {
      log::Write("redirected CurrentApp ICurrentAppWithConsumables");
      return ReturnSingleton(&g_current_app_consumables, factory);
    }

    for (const Entry& entry : kRedirects) {
      if (::lstrcmpW(class_name, entry.class_name) == 0) {
        LogClassName("redirected", class_name);
        return ReturnSingleton(entry.resolve(), factory);
      }
    }

    for (const wchar_t* pending : kPending) {
      if (pending != nullptr && ::lstrcmpW(class_name, pending) == 0) {
        LogClassName("PENDING activation (falling through):", class_name);
        break;
      }
    }
  }

  // Todo lo demas al sistema. En Windows 8 y posteriores muchas de estas clases
  // existen de verdad, y suplantarlas seria peor que dejarlas pasar.
  if (!allow_fallback || g_fallback == nullptr) {
    return REGDB_E_CLASSNOTREG;
  }

  const HRESULT result = g_fallback(class_name, iid, factory);
  if (FAILED(result)) {
    // Solo se traza el fallo: el camino bueno pasa por aqui cientos de veces
    // durante la carga y llenaria la salida de depuracion.
    LogClassName("WinRT fallback failed for", class_name);
    log::WriteHResult("  fallback", result);
  }
  return result;
}

HRESULT SHIM_COM GetActivationFactoryByName(const wchar_t* class_name,
                                            const GUID* iid,
                                            void** factory) noexcept {
  return ResolveActivationFactoryByName(class_name, iid, factory, true);
}

HRESULT GetActivationFactoryByHString(HSTRING class_name, const GUID* iid,
                                      void** factory) noexcept {
  if (factory == nullptr) {
    return E_POINTER;
  }
  *factory = nullptr;
  if (class_name == nullptr || iid == nullptr) {
    return E_INVALIDARG;
  }

  UINT32 length = 0;
  const wchar_t* const raw =
      winrt_string::GetRawBuffer(class_name, &length);
  if (raw == nullptr || length == 0) {
    return REGDB_E_CLASSNOTREG;
  }

  return ResolveActivationFactoryByName(raw, iid, factory, false);
}

HRESULT ActivateInstanceByHString(HSTRING class_name, void** instance) noexcept {
  if (instance == nullptr) {
    return E_POINTER;
  }
  *instance = nullptr;

  void* factory = nullptr;
  HRESULT hr =
      GetActivationFactoryByHString(class_name, &kIidIActivationFactory,
                                    &factory);
  if (FAILED(hr)) {
    return hr;
  }
  if (factory == nullptr) {
    return E_NOINTERFACE;
  }

  if (!memory::IsReadable(factory, sizeof(void*))) {
    return E_NOINTERFACE;
  }
  void** const vtable = *reinterpret_cast<void***>(factory);
  if (vtable == nullptr ||
      !memory::IsReadable(vtable, sizeof(void*) * 7) ||
      !memory::IsExecutable(vtable[2])) {
    return E_NOINTERFACE;
  }

  const auto release = reinterpret_cast<ReleaseFn>(vtable[2]);
  if (!memory::IsExecutable(vtable[6])) {
    release(factory);
    return E_NOINTERFACE;
  }

  const auto activate = reinterpret_cast<ActivateInstanceFn>(vtable[6]);
  hr = activate(factory, instance);
  release(factory);
  return hr;
}

}  // namespace shim::winrt
