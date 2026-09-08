#include "app_model_internal.h"
#include "shim/winrt_string_compat.h"

#include "shim/storage.h"

#include <winstring.h>

namespace shim::winrt {
namespace {

// --- Metodos compartidos --------------------------------------------------

// Devuelve el StorageFolder de instalacion (sub_6294AC10).
//
// Es de donde el juego lee sus recursos: el directorio del EXE.
HRESULT SHIM_COM GetInstalledLocation(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(storage::InstalledLocationFolder(), out);
}

// Devuelve el LocalFolder (sub_6294AC40).
//
// Las tres carpetas de ApplicationData (local, roaming y temporal) devuelven la
// MISMA. Fuera de UWP no hay sincronizacion en la nube ni limpieza automatica,
// asi que separarlas solo repartiria los datos del jugador por tres sitios.
HRESULT SHIM_COM GetLocalFolder(Object* self, void** out) noexcept {
  (void)self;
  return ReturnSingleton(storage::LocalFolderObject(), out);
}

// --- MemoryManager --------------------------------------------------------

HRESULT SHIM_COM GetMemoryLimit(Object* self, unsigned int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  // UINT64 partido en dos palabras: la parte alta es cero porque 1,5 GiB cabe
  // de sobra en 32 bits.
  out[0] = kMemoryLimitBytes;
  out[1] = 0;
  return S_OK;
}

// --- CoreApplication ------------------------------------------------------

HRESULT SHIM_COM GetApplicationId(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(kApplicationId, kApplicationIdLength, out);
}


#define SHIM_IINSPECTABLE                              \
  reinterpret_cast<Method>(&QueryInterface),           \
      reinterpret_cast<Method>(&AddRef),               \
      reinterpret_cast<Method>(&Release),              \
      reinterpret_cast<Method>(&GetIids),              \
      reinterpret_cast<Method>(&GetRuntimeClassName),  \
      reinterpret_cast<Method>(&GetTrustLevel)

// IPackage (0x629930D0): get_Id nulo, get_InstalledLocation en el 7.
Method g_package_vtable[] = {
    SHIM_IINSPECTABLE,
    nullptr,
    reinterpret_cast<Method>(&GetInstalledLocation),
};

// IApplicationData (0x629930F0): las tres carpetas al final, del 12 al 14.
Method g_application_data_vtable[] = {
    SHIM_IINSPECTABLE,
    nullptr,  //  6 get_Version
    nullptr,  //  7 SetVersionAsync
    nullptr,  //  8 ClearAllAsync
    nullptr,  //  9 ClearAsync
    nullptr,  // 10 get_LocalSettings
    nullptr,  // 11 get_RoamingSettings
    reinterpret_cast<Method>(&GetLocalFolder),      // 12 LocalFolder
    reinterpret_cast<Method>(&GetLocalFolder),      // 13 RoamingFolder
    reinterpret_cast<Method>(&GetLocalFolder),      // 14 TemporaryFolder
};

// IMemoryManagerStatics (0x62993020): limite y uso, la misma respuesta.
Method g_memory_manager_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&GetMemoryLimit),
    reinterpret_cast<Method>(&GetMemoryLimit),
};

// ICoreApplication (0x629930A0): solo get_Id.
Method g_core_application_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&GetApplicationId),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

Object g_package_factory = {g_activation_factory_vtable, 1, kPackageFactory};
Object g_application_data_factory = {g_activation_factory_vtable, 1,
                                     kApplicationDataFactory};
Object g_package = {g_package_vtable, 1, kPackage};
Object g_application_data = {g_application_data_vtable, 1, kApplicationData};
Object g_memory_manager = {g_memory_manager_vtable, 1, kMemoryManager};
Object g_core_application = {g_core_application_vtable, 1, kCoreApplication};

}  // namespace

Object* PackageFactory() noexcept { return &g_package_factory; }
Object* ApplicationDataFactory() noexcept { return &g_application_data_factory; }
Object* MemoryManagerStatics() noexcept { return &g_memory_manager; }
Object* CoreApplicationStatics() noexcept { return &g_core_application; }

namespace app_model_detail {

Object* PackageInstance() noexcept { return &g_package; }
Object* ApplicationDataInstance() noexcept { return &g_application_data; }

}  // namespace app_model_detail

#undef SHIM_IINSPECTABLE

}  // namespace shim::winrt
