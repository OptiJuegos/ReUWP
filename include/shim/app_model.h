// Las clases WinRT del modelo de aplicacion, falsificadas sobre Win32.
//
// Original: sub_6294A950 (ActivateInstance compartido), sub_6294A9C0
// (MemoryManager), sub_6294ABF0 (CoreApplication.Id), sub_6294AC10/AC40
// (carpetas), sub_6294C3E0..C440 (idiomas), sub_6294C5F0 (region),
// sub_6294C750/C810 (XAML), sub_6294DD70 (Launcher), sub_6294E930/E940
// (CachedFileManager) y sub_6294ED40..ED70 (ThreadPool).
//
// Son las clases que el juego pide durante el arranque para averiguar donde
// esta instalado, donde puede escribir, en que idioma va y cuanta memoria tiene.
// Ninguna hace nada interesante: todas responden lo minimo para que el juego
// siga adelante. Su valor esta en existir, no en lo que devuelven.
//
//
// EL PATRON DE ESTE FICHERO: FABRICA -> INSTANCIA
//
// En WinRT una clase con miembros de instancia se pide en dos pasos. Primero se
// obtiene la fabrica (IActivationFactory), y a esa se le pide la instancia con
// ActivateInstance. El binario tiene UN SOLO ActivateInstance para todas, que
// mira el `kind` de la fabrica y devuelve la instancia que toca. Aqui igual.

#pragma once

#include "shim/winrt_object.h"

namespace shim::winrt {

// Etiquetas leidas de la tabla de singletons del binario (0x62997570).
//
// Las fabricas y sus instancias van en pares: la fabrica lleva un `kind` y la
// instancia otro, y ActivateInstance traduce de uno a otro.
enum AppModelKind : int {
  kPackageFactory = 0,
  kApplicationDataFactory = 1,
  kMemoryManager = 2,
  kCoreApplication = 5,
  kPackage = 6,             // instancia que produce kPackageFactory
  kApplicationData = 7,     // instancia que produce kApplicationDataFactory
  kGenericInstance = 8,     // instancia de cualquier fabrica no reconocida
  kApplicationLanguages = 32,
  kLanguageVector = 33,
  kGeographicRegionStatics = 34,
  kGeographicRegion = 35,
  kCurrencyVector = 36,
  kXamlApplicationStatics = 37,
  kXamlApplication = 38,
  kXamlWindowStatics = 39,
  kXamlWindow = 40,
  kLauncher = 67,
  kLauncherOptionsFactory = 68,
  kLauncherOptions = 69,
  kCachedFileManager = 80,
};

// Identidad que se reporta como CoreApplication::Id (sub_6294ABF0).
//
// Un paquete UWP tiene un nombre de familia; esto no es un paquete. El valor es
// libre porque el juego solo lo usa para nombrar ficheros de estado.
inline constexpr wchar_t kApplicationId[] = L"Minecraft.Win32";
inline constexpr UINT32 kApplicationIdLength = 15;

// Memoria que se reporta como limite y como uso (sub_6294A9C0).
//
// 0x60000000 son 1,5 GiB, que es el limite real de un proceso de 32 bits con
// LARGEADDRESSAWARE en la practica. El juego lo usa para decidir cuanta cache de
// chunks mantener: decir mas le haria reservar memoria que no puede direccionar.
inline constexpr unsigned int kMemoryLimitBytes = 0x60000000;

// Fabricas de activacion, las que devuelve el gancho de activacion.
Object* PackageFactory() noexcept;
Object* ApplicationDataFactory() noexcept;
Object* LauncherOptionsFactory() noexcept;
Object* MemoryManagerStatics() noexcept;
Object* CoreApplicationStatics() noexcept;
Object* ApplicationLanguagesStatics() noexcept;
Object* LanguageVectorSingleton() noexcept;
Object* GeographicRegionStatics() noexcept;
Object* XamlApplicationStatics() noexcept;
Object* XamlWindowStatics() noexcept;
// Objeto que sustituye Window.Content en 1.1.5. Su interfaz minima expone la
// escala de rasterizacion que AppMainXaml consulta durante resize.
Object* XamlSizeSourceSingleton() noexcept;

// Registra el AppMain directo para que el size-source XAML pueda reproducir
// UIElement.ActualSize. El original consulta dword_62998EF8 desde el getter.
void SetXamlAppMain(void* app_main) noexcept;
Object* LauncherStatics() noexcept;
Object* CachedFileManagerStatics() noexcept;
Object* ThreadPoolStatics() noexcept;

// IActivationFactory::ActivateInstance compartido (sub_6294A950).
//
// Mira el `kind` de la fabrica y devuelve la instancia correspondiente. Una
// fabrica que no reconoce produce la instancia generica, que responde a todo con
// valores neutros en vez de fallar.
HRESULT SHIM_COM ActivateInstance(Object* self, void** instance) noexcept;

}  // namespace shim::winrt
