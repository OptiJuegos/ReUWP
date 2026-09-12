#include "app_model_internal.h"
#include "shim/winrt_string_compat.h"

#include "shim/storage.h"
#include "shim/app_window.h"

#define PSAPI_VERSION 1
#include <psapi.h>
#include <cstring>
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

void StoreUint64(unsigned long long value, unsigned int* out) noexcept {
  out[0] = static_cast<unsigned int>(value);
  out[1] = static_cast<unsigned int>(value >> 32);
}

HRESULT SHIM_COM GetAppMemoryUsage(Object* self, unsigned int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }

  PROCESS_MEMORY_COUNTERS_EX counters = {};
  counters.cb = sizeof(counters);
  if (!::GetProcessMemoryInfo(
          ::GetCurrentProcess(),
          reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
          sizeof(counters))) {
    StoreUint64(0, out);
    return S_OK;
  }

  StoreUint64(static_cast<unsigned long long>(counters.PrivateUsage), out);
  return S_OK;
}

unsigned long long QueryAppMemoryUsageLimit() noexcept {
  MEMORYSTATUSEX status = {};
  status.dwLength = sizeof(status);
  if (::GlobalMemoryStatusEx(&status)) {
    // AppMemoryUsageLimit is a budget, not the amount currently free. Report
    // the smaller of installed physical RAM and this process' virtual address
    // space. Bedrock subtracts AppMemoryUsage itself and applies its own cap.
    if (status.ullTotalPhys == 0) {
      return status.ullTotalVirtual;
    }
    if (status.ullTotalVirtual == 0) {
      return status.ullTotalPhys;
    }
    return status.ullTotalPhys < status.ullTotalVirtual
               ? status.ullTotalPhys
               : status.ullTotalVirtual;
  }

  // GlobalMemoryStatusEx exists on every supported Windows target, but keep a
  // dynamic fallback based on the actual user-mode address range instead of a
  // hard-coded quota.
  SYSTEM_INFO info = {};
  ::GetSystemInfo(&info);
  const uintptr_t first =
      reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
  const uintptr_t last =
      reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
  return last >= first ? static_cast<unsigned long long>(last - first) + 1ull
                       : 0ull;
}

HRESULT SHIM_COM GetAppMemoryUsageLimit(Object* self,
                                        unsigned int* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  StoreUint64(QueryAppMemoryUsageLimit(), out);
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

HRESULT SHIM_COM ExitCoreApplication(Object* self) noexcept {
  (void)self;
  return app_window::RequestClose() ? S_OK
                                    : HRESULT_FROM_WIN32(::GetLastError());
}

HRESULT SHIM_COM AddCoreApplicationExiting(Object* self, void* handler,
                                           INT64* token) noexcept {
  (void)self;
  (void)handler;
  if (token == nullptr) {
    return E_POINTER;
  }
  *token = 0;
  return S_OK;
}

HRESULT SHIM_COM RemoveCoreApplicationExiting(Object* self,
                                              INT64 token) noexcept {
  (void)self;
  (void)token;
  return S_OK;
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

// IMemoryManagerStatics (0x62993020): current usage followed by usage limit.
Method g_memory_manager_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&GetAppMemoryUsage),
    reinterpret_cast<Method>(&GetAppMemoryUsageLimit),
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

Method g_core_application_exit_vtable[] = {
    SHIM_IINSPECTABLE,
    reinterpret_cast<Method>(&ExitCoreApplication),
    reinterpret_cast<Method>(&AddCoreApplicationExiting),
    reinterpret_cast<Method>(&RemoveCoreApplicationExiting),
};
static_assert(CountOf(g_core_application_exit_vtable) == 9,
              "ICoreApplicationExit must expose 9 slots");

Object g_package_factory = {g_activation_factory_vtable, 1, kPackageFactory};
Object g_application_data_factory = {g_activation_factory_vtable, 1,
                                     kApplicationDataFactory};
Object g_package = {g_package_vtable, 1, kPackage};
Object g_application_data = {g_application_data_vtable, 1, kApplicationData};
Object g_memory_manager = {g_memory_manager_vtable, 1, kMemoryManager};
Object g_core_application = {g_core_application_vtable, 1, kCoreApplication};
Object g_core_application_exit = {g_core_application_exit_vtable, 1,
                                  kCoreApplication};

}  // namespace

Object* PackageFactory() noexcept { return &g_package_factory; }
Object* ApplicationDataFactory() noexcept { return &g_application_data_factory; }
Object* MemoryManagerStatics() noexcept { return &g_memory_manager; }
Object* CoreApplicationStatics() noexcept { return &g_core_application; }

Object* CoreApplicationForInterface(const IID* iid) noexcept {
  if (iid != nullptr &&
      memcmp(iid, &kCoreApplicationExitIid, sizeof(GUID)) == 0) {
    return &g_core_application_exit;
  }
  return &g_core_application;
}

namespace app_model_detail {

Object* PackageInstance() noexcept { return &g_package; }
Object* ApplicationDataInstance() noexcept { return &g_application_data; }

}  // namespace app_model_detail

#undef SHIM_IINSPECTABLE

}  // namespace shim::winrt
