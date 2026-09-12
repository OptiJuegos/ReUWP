#include "shim/storage.h"
#include "shim/winrt_string_compat.h"
#include "shim/runtime_trace.h"

#include <winstring.h>

#include <cstring>

#include "shim/file_picker.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/local_folder.h"
#include "shim/log.h"
#include "shim/store.h"
#include "shim/winrt_vtable.h"

namespace shim::storage {
namespace {

wchar_t g_local_folder[kPathCapacity] = {};  // word_62997368
wchar_t g_resolved_path[kPathCapacity] = {}; // dword_62998420
bool g_file_operation_ready = false;         // byte_62998E8C

// IID_Windows_Storage_IStorageItem. El original lo compara contra los 16 bytes
// en 0x62989168 dentro de StorageFile::QueryInterface.
constexpr GUID kIidStorageItem = {
    0x4207A996, 0xCA2F, 0x42F7,
    {0xBD, 0xE8, 0x8B, 0x10, 0x45, 0x7A, 0x7F, 0x30}};

// IStorageFolder. El original usa dos formas segun version:
//
//   1.1.5  @ 0x629931F4: 13 entradas, CreateFileAsync en slot 7 y get_Path en 12.
//   0.15.10 @ 0x62993228: misma forma, pero CreateFileAsync queda NULL.
//
// InstalledLocation y LocalFolder comparten la misma vtable; solo cambia `kind`
// (23/24), que es precisamente lo que get_Path usa para elegir la ruta.
winrt::Method g_storage_folder_vtable_115[] = {
    reinterpret_cast<winrt::Method>(&winrt::QueryInterface),
    reinterpret_cast<winrt::Method>(&winrt::AddRef),
    reinterpret_cast<winrt::Method>(&winrt::Release),
    reinterpret_cast<winrt::Method>(&winrt::GetIids),
    reinterpret_cast<winrt::Method>(&winrt::GetRuntimeClassName),
    reinterpret_cast<winrt::Method>(&winrt::GetTrustLevel),
    nullptr,
    reinterpret_cast<winrt::Method>(&CreateFileAsync),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    reinterpret_cast<winrt::Method>(&GetFolderPath),
};
static_assert(CountOf(g_storage_folder_vtable_115) == 13,
              "1.1.5 StorageFolder vtable must have 13 entries");

winrt::Method g_storage_folder_vtable_01510[] = {
    reinterpret_cast<winrt::Method>(&winrt::QueryInterface),
    reinterpret_cast<winrt::Method>(&winrt::AddRef),
    reinterpret_cast<winrt::Method>(&winrt::Release),
    reinterpret_cast<winrt::Method>(&winrt::GetIids),
    reinterpret_cast<winrt::Method>(&winrt::GetRuntimeClassName),
    reinterpret_cast<winrt::Method>(&winrt::GetTrustLevel),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    reinterpret_cast<winrt::Method>(&GetFolderPath),
};
static_assert(CountOf(g_storage_folder_vtable_01510) == 13,
              "0.15.10 StorageFolder vtable must have 13 entries");

Object g_installed_folder_115 = {g_storage_folder_vtable_115, 1,
                                 kInstalledLocationKind};
Object g_local_folder_115 = {g_storage_folder_vtable_115, 1,
                             kLocalFolderKind};
Object g_installed_folder_01510 = {g_storage_folder_vtable_01510, 1,
                                   kInstalledLocationKind};
Object g_local_folder_01510 = {g_storage_folder_vtable_01510, 1,
                               kLocalFolderKind};

// StorageFile expone dos proyecciones COM sobre el mismo fichero:
// IStorageFile (vtable 0x62993BD0) e IStorageItem (0x62993C18).
//
// El slot 0 NO usa el QueryInterface generico: el original usa 0x6294E4E0
// para devolver una de las dos proyecciones segun el IID. Si se devuelve el
// IStorageFile para un IID de IStorageItem, el juego empieza a llamar slots de
// IStorageItem sobre la vtable equivocada y el fallo aparece mucho despues.
HRESULT SHIM_COM StorageFileQueryInterface(Object* self, const IID* iid,
                                            void** out) noexcept;
HRESULT SHIM_COM StorageItemRenameAsync(Object* self, HSTRING name,
                                         void** operation) noexcept;
HRESULT SHIM_COM StorageItemRenameAsyncWithOption(Object* self, HSTRING name,
                                                   int option,
                                                   void** operation) noexcept;
HRESULT SHIM_COM StorageItemDeleteAsync(Object* self,
                                         void** operation) noexcept;
HRESULT SHIM_COM StorageItemDeleteAsyncWithOption(Object* self, int option,
                                                   void** operation) noexcept;
HRESULT SHIM_COM StorageItemGetBasicPropertiesAsync(Object* self,
                                                     void** operation) noexcept;
HRESULT SHIM_COM GetStorageItemName(Object* self, HSTRING* out) noexcept;
HRESULT SHIM_COM GetStorageItemPath(Object* self, HSTRING* out) noexcept;
HRESULT SHIM_COM GetStorageItemAttributes(Object* self, DWORD* out) noexcept;
HRESULT SHIM_COM GetStorageItemDateCreated(Object* self,
                                            LONGLONG* out) noexcept;
HRESULT SHIM_COM StorageItemIsOfType(Object* self, int type,
                                      BYTE* out) noexcept;

// Vtable IStorageFile, exacta en longitud y orden a 0x62993BD0.
winrt::Method g_storage_file_vtable[] = {
    reinterpret_cast<winrt::Method>(&StorageFileQueryInterface),  //  0 E4E0
    reinterpret_cast<winrt::Method>(&winrt::AddRef),              //  1
    reinterpret_cast<winrt::Method>(&winrt::Release),             //  2
    reinterpret_cast<winrt::Method>(&winrt::GetIids),             //  3
    reinterpret_cast<winrt::Method>(&winrt::GetRuntimeClassName), //  4
    reinterpret_cast<winrt::Method>(&winrt::GetTrustLevel),       //  5
    reinterpret_cast<winrt::Method>(&GetFileType),                 //  6 E550
    reinterpret_cast<winrt::Method>(&GetContentType),              //  7 E5C0
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup), //  8
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup), //  9
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup), // 10
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup), // 11
    reinterpret_cast<winrt::Method>(&CopyAsync),                   // 12 E5E0
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup), // 13
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup), // 14
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup), // 15
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup), // 16
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup), // 17
};
static_assert(CountOf(g_storage_file_vtable) == 18,
              "StorageFile vtable must match 0x62993BD0");

// IStorageItem projection, 16 entradas exactas a 0x62993C18. Los slots
// 6..10 apuntan todos al stub 0x6294E040 del original (E_NOTIMPL + RET sin
// limpieza), no a cinco metodos __stdcall distintos.
winrt::Method g_storage_item_vtable[] = {
    reinterpret_cast<winrt::Method>(&StorageFileQueryInterface),       //  0
    reinterpret_cast<winrt::Method>(&winrt::AddRef),                   //  1
    reinterpret_cast<winrt::Method>(&winrt::Release),                  //  2
    reinterpret_cast<winrt::Method>(&winrt::GetIids),                  //  3
    reinterpret_cast<winrt::Method>(&winrt::GetRuntimeClassName),      //  4
    reinterpret_cast<winrt::Method>(&winrt::GetTrustLevel),            //  5
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup),  //  6
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup),  //  7
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup),  //  8
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup),  //  9
    reinterpret_cast<winrt::Method>(&winrt::NotImplementedNoCleanup),  // 10
    reinterpret_cast<winrt::Method>(&GetStorageItemName),              // 11 E820
    reinterpret_cast<winrt::Method>(&GetStorageItemPath),              // 12 E890
    reinterpret_cast<winrt::Method>(&GetStorageItemAttributes),        // 13 E8E0
    reinterpret_cast<winrt::Method>(&GetStorageItemDateCreated),       // 14 E900
    reinterpret_cast<winrt::Method>(&StorageItemIsOfType),             // 15 E930
};
static_assert(CountOf(g_storage_item_vtable) == 16,
              "StorageItem vtable must match 0x62993C18");

// Vtable de las dos operaciones asincronas de seleccion. Comparte forma con la
// de las operaciones de la tienda (0x62993484).
winrt::Method g_picker_operation_vtable[] = {
    reinterpret_cast<winrt::Method>(&winrt::QueryInterface),
    reinterpret_cast<winrt::Method>(&winrt::AddRef),
    reinterpret_cast<winrt::Method>(&winrt::Release),
    reinterpret_cast<winrt::Method>(&winrt::GetIids),
    reinterpret_cast<winrt::Method>(&winrt::GetRuntimeClassName),
    reinterpret_cast<winrt::Method>(&winrt::GetTrustLevel),
    reinterpret_cast<winrt::Method>(&store::SetAsyncCompletedHandler),
    reinterpret_cast<winrt::Method>(&store::GetAsyncCompletedHandler),
    reinterpret_cast<winrt::Method>(&store::GetAsyncResults),
};

// El original mantiene dos StorageFile y tres operaciones con kind 77:
// - save/open picker devuelven el fichero elegido por el dialogo (String1),
// - CreateFileAsync/CopyAsync devuelven otra operacion cuyo resultado apunta al
//   fichero resuelto dentro de LocalFolder (dword_62998420).
//
// Las dos operaciones de picker son objetos distintos por identidad, aunque
// comparten el mismo estado de exito del ultimo dialogo (dword_62998E88).
// El original mantiene una pareja IStorageFile/IStorageItem para la ruta del
// picker y otra pareja para la ruta resuelta dentro de LocalFolder.
Object g_picked_file = {g_storage_file_vtable, 1, kStorageFileKind};
Object g_picked_storage_item = {g_storage_item_vtable, 1, kStorageItemKind};
Object g_resolved_file = {g_storage_file_vtable, 1, kStorageFileKind};
Object g_resolved_storage_item = {g_storage_item_vtable, 1, kStorageItemKind};
Object g_open_picker_operation = {g_picker_operation_vtable, 1,
                                  kPickerOperationKind};
Object g_save_picker_operation = {g_picker_operation_vtable, 1,
                                  kPickerOperationKind};
Object g_file_operation = {g_picker_operation_vtable, 1, kPickerOperationKind};

// Copia el contenido de un fichero a otro por bloques.
//
// Se usa ReadFile/WriteFile en bucle en vez de CopyFileW porque el destino ya
// esta creado y abierto con los permisos que interesan; ademas el bucle de
// escritura contempla escrituras parciales, que CopyFileW oculta.
bool CopyFileContents(HANDLE source, HANDLE destination) noexcept {
  unsigned char buffer[kCopyBlockSize];

  for (;;) {
    DWORD read = 0;
    if (!::ReadFile(source, buffer, kCopyBlockSize, &read, nullptr)) {
      return false;
    }
    if (read == 0) {
      return true;  // fin de fichero
    }

    // ReadFile puede devolver menos de lo pedido y WriteFile escribir menos de
    // lo dado; el bucle interno cubre lo segundo.
    DWORD written_total = 0;
    while (written_total < read) {
      DWORD written = 0;
      if (!::WriteFile(destination, buffer + written_total,
                       read - written_total, &written, nullptr) ||
          written == 0) {
        return false;
      }
      written_total += written;
    }
  }
}

// Ruta desde la que este objeto describe su fichero.
//
// El de exportacion habla del fichero que el shim acaba de crear dentro del
// LocalFolder; el de importacion, del que eligio el usuario en el dialogo.
const wchar_t* PathForFile(const Object* file) noexcept {
  const bool resolved =
      file == &g_resolved_file || file == &g_resolved_storage_item;
  return resolved ? g_resolved_path : shim::storage::LastPickedPath();
}

HRESULT SHIM_COM StorageFileQueryInterface(Object* self, const IID* iid,
                                            void** out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;

  // 0x6294E4E0 mantiene la identidad del fichero pero cambia de proyeccion.
  const bool resolved =
      self == &g_resolved_file || self == &g_resolved_storage_item;
  const bool wants_storage_item =
      iid != nullptr &&
      std::memcmp(iid, &kIidStorageItem, sizeof(kIidStorageItem)) == 0;

  Object* result = nullptr;
  if (wants_storage_item) {
    result = resolved ? &g_resolved_storage_item : &g_picked_storage_item;
  } else {
    result = resolved ? &g_resolved_file : &g_picked_file;
  }

  *out = result;
  winrt::AddRef(result);
  return S_OK;
}

HRESULT SHIM_COM StorageItemRenameAsync(Object* self, HSTRING name,
                                         void** operation) noexcept {
  (void)self;
  (void)name;
  if (operation != nullptr) {
    *operation = nullptr;
  }
  return E_NOTIMPL;
}

HRESULT SHIM_COM StorageItemRenameAsyncWithOption(Object* self, HSTRING name,
                                                   int option,
                                                   void** operation) noexcept {
  (void)self;
  (void)name;
  (void)option;
  if (operation != nullptr) {
    *operation = nullptr;
  }
  return E_NOTIMPL;
}

HRESULT SHIM_COM StorageItemDeleteAsync(Object* self,
                                         void** operation) noexcept {
  (void)self;
  if (operation != nullptr) {
    *operation = nullptr;
  }
  return E_NOTIMPL;
}

HRESULT SHIM_COM StorageItemDeleteAsyncWithOption(Object* self, int option,
                                                   void** operation) noexcept {
  (void)self;
  (void)option;
  if (operation != nullptr) {
    *operation = nullptr;
  }
  return E_NOTIMPL;
}

HRESULT SHIM_COM StorageItemGetBasicPropertiesAsync(Object* self,
                                                     void** operation) noexcept {
  (void)self;
  if (operation != nullptr) {
    *operation = nullptr;
  }
  return E_NOTIMPL;
}

// 0x6294E820: IStorageItem::get_Name.
HRESULT SHIM_COM GetStorageItemName(Object* self, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  const wchar_t* path = PathForFile(self);
  const wchar_t* name = path;
  for (const wchar_t* cursor = path; *cursor != L'\0'; ++cursor) {
    if (*cursor == L'\\' || *cursor == L'/') {
      name = cursor + 1;
    }
  }
  return shim::winrt_string::Create(
      name, static_cast<UINT32>(::lstrlenW(name)), out);
}

// 0x6294E890: IStorageItem::get_Path.
HRESULT SHIM_COM GetStorageItemPath(Object* self, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  const wchar_t* path = PathForFile(self);
  return shim::winrt_string::Create(
      path, static_cast<UINT32>(::lstrlenW(path)), out);
}

// 0x6294E8E0.
HRESULT SHIM_COM GetStorageItemAttributes(Object* self, DWORD* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

// 0x6294E900 devuelve DateCreated=0.
HRESULT SHIM_COM GetStorageItemDateCreated(Object* self,
                                            LONGLONG* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

// 0x6294E930: StorageItemTypes::File vale 0 en el camino que usa el juego.
HRESULT SHIM_COM StorageItemIsOfType(Object* self, int type,
                                      BYTE* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = type == 0 ? 1 : 0;
  return S_OK;
}

}  // namespace

Object* InstalledLocationFolder() noexcept {
  return game::HasCapability(game::VersionCapability::kStorageV2Projection)
             ? &g_installed_folder_115
             : &g_installed_folder_01510;
}

Object* LocalFolderObject() noexcept {
  return game::HasCapability(game::VersionCapability::kStorageV2Projection)
             ? &g_local_folder_115
             : &g_local_folder_01510;
}

HRESULT SHIM_COM GetFolderPath(Object* self, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;

  const wchar_t* path = g_local_folder;
  if (self != nullptr && self->kind == kInstalledLocationKind) {
    path = local_folder::InstallDirectory();
  }
  if (path == nullptr) {
    path = L"";
  }

  return shim::winrt_string::Create(
      path, static_cast<UINT32>(::lstrlenW(path)), out);
}

Object* ImportOperation() noexcept {
  return &g_open_picker_operation;
}

Object* ExportOperation() noexcept {
  return &g_save_picker_operation;
}

Object* PickerResult(const Object* operation) noexcept {
  // CreateFileAsync/CopyAsync usan una operacion distinta. En el original
  // GetResults la reconoce por identidad (&dword_62997BD0) y consulta el byte
  // 62998E8C antes de devolver el StorageFile ligado a g_resolved_path.
  if (operation == &g_file_operation) {
    return g_file_operation_ready ? &g_resolved_file : nullptr;
  }

  // SavePicker y OpenPicker comparten dword_62998E88. RecordResult() ya mantiene
  // exactamente ese estado en file_picker.cpp, asi que no se duplica aqui.
  return shim::storage::LastPickSucceeded() ? &g_picked_file : nullptr;
}

HRESULT SHIM_COM GetFileType(Object* self, HSTRING* out) noexcept {
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;

  const wchar_t* path = PathForFile(self);
 
  // Se busca el ULTIMO punto, no el primero: "world.backup.mcworld" tiene
  // extension .mcworld, no .backup.mcworld.
  const wchar_t* extension = nullptr;
  for (const wchar_t* cursor = path; *cursor != L'\0'; ++cursor) {
    if (*cursor == L'.') {
      extension = cursor;
    }
  }
  if (extension == nullptr) {
    // Sin extension se devuelve cadena vacia, no un fallo: un fichero sin punto
    // es raro pero valido, y WinRT representa eso con un HSTRING vacio.
    return S_OK;
  }

  return shim::winrt_string::Create(
      extension, static_cast<UINT32>(::lstrlenW(extension)), out);
}

HRESULT SHIM_COM GetContentType(Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(L"application/octet-stream", 24, out);
}

void SetLocalFolder(const wchar_t* path) noexcept {
  if (path == nullptr) {
    g_local_folder[0] = L'\0';
    return;
  }
  ::lstrcpynW(g_local_folder, path, static_cast<int>(kPathCapacity));
}

const wchar_t* LocalFolder() noexcept {
  return g_local_folder;
}

const wchar_t* LastPickedSourcePath() noexcept {
  // El origen de las importaciones es siempre lo que eligio el usuario en el
  // dialogo de apertura (String1 en el original, compartido entre modulos).
  return shim::storage::LastPickedPath();
}

bool ResolveInLocalFolder(HSTRING relative) noexcept {
  g_resolved_path[0] = L'\0';
  if (relative == nullptr) {
    return false;
  }

  UINT32 length = 0;
  const wchar_t* text = shim::winrt_string::GetRawBuffer(relative, &length);
  if (text == nullptr || length == 0) {
    return false;
  }

  const int base_length = ::lstrlenW(g_local_folder);
  if (static_cast<size_t>(base_length) + length > kMaxJoinedLength) {
    return false;
  }

  ::lstrcpyW(g_resolved_path, g_local_folder);

  size_t offset = static_cast<size_t>(base_length);
  if (offset != 0) {
    // Se inserta separador solo si la base no lo trae ya, para no generar
    // rutas con doble barra.
    const wchar_t last = g_resolved_path[offset - 1];
    if (last != L'\\' && last != L'/') {
      g_resolved_path[offset] = L'\\';
      ++offset;
    }
  }

  memcpy(g_resolved_path + offset, text, sizeof(wchar_t) * length);
  g_resolved_path[offset + length] = L'\0';
  return true;
}

HRESULT SHIM_COM CreateFileAsync(Object* self, HSTRING name,
                                 int collision_option,
                                 void** operation) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                        "Storage.CreateFileAsync.begin",
                        reinterpret_cast<uintptr_t>(self),
                        reinterpret_cast<uintptr_t>(name));
  (void)self;
  (void)collision_option;
  if (operation == nullptr) {
    runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                          "Storage.CreateFileAsync.end", 0, 0, E_POINTER);
    return E_POINTER;
  }
  *operation = nullptr;
  g_file_operation_ready = false;

  if (!ResolveInLocalFolder(name)) {
    runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                          "Storage.CreateFileAsync.end", 0, 0, E_FAIL);
    return E_FAIL;
  }

  const HANDLE file = ::CreateFileW(
      g_resolved_path, GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    g_resolved_path[0] = L'\0';
    runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                          "Storage.CreateFileAsync.end", 0, 0, E_FAIL);
    return E_FAIL;
  }
  ::CloseHandle(file);

  g_file_operation_ready = true;
  log::Write("IStorageFolder::CreateFileAsync created local temp file");
  const HRESULT result = winrt::ReturnSingleton(&g_file_operation, operation);
  runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                        "Storage.CreateFileAsync.end",
                        reinterpret_cast<uintptr_t>(&g_file_operation), 0,
                        result);
  return result;
}

HRESULT SHIM_COM CopyAsync(Object* self, void* destination_folder, HSTRING name,
                           int collision_option, void** operation) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                        "Storage.CopyAsync.begin",
                        reinterpret_cast<uintptr_t>(self),
                        reinterpret_cast<uintptr_t>(name));
  (void)self;
  (void)destination_folder;
  (void)collision_option;
  if (operation == nullptr) {
    runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                          "Storage.CopyAsync.end", 0, 0, E_POINTER);
    return E_POINTER;
  }
  *operation = nullptr;
  g_file_operation_ready = false;

  // El origen es la ruta que dejo el ultimo dialogo de apertura.
  //
  // El original elige entre esa ruta y el buffer de ruta resuelta segun que
  // objeto sea `this`, pero esa segunda rama esta MUERTA: el buffer resuelto se
  // limpia unas lineas antes de comprobar si el origen esta vacio, asi que
  // cuando apunta ahi la comprobacion siempre da vacio y se sale con error. En
  // la practica el origen es siempre la ruta del dialogo, y asi se reconstruye.
  // Reproducir la rama muerta solo anadiria un camino que nunca se toma.
  const wchar_t* source_path = shim::storage::LastPickedSourcePath();
  if (source_path == nullptr || source_path[0] == L'\0') {
    runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                          "Storage.CopyAsync.end", 0, 0, E_FAIL);
    return E_FAIL;
  }

  // Se copia antes de resolver el destino: ResolveInLocalFolder machaca el
  // buffer de ruta resuelta, y en algunos caminos el origen sale de ahi.
  wchar_t source_copy[kPathCapacity];
  ::lstrcpynW(source_copy, source_path, static_cast<int>(kPathCapacity));

  if (!ResolveInLocalFolder(name)) {
    runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                          "Storage.CopyAsync.end", 0, 0, E_FAIL);
    return E_FAIL;
  }

  // Si el fichero ya esta donde debe, no hay nada que copiar. Sin esta guarda
  // se abriria el mismo fichero para lectura y escritura y se truncaria.
  if (::lstrcmpW(source_copy, g_resolved_path) == 0) {
    g_file_operation_ready = true;
    const HRESULT result = winrt::ReturnSingleton(&g_file_operation, operation);
    runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                          "Storage.CopyAsync.end",
                          reinterpret_cast<uintptr_t>(&g_file_operation), 0,
                          result);
    return result;
  }

  const HANDLE source = ::CreateFileW(
      source_copy, GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (source == INVALID_HANDLE_VALUE) {
    g_resolved_path[0] = L'\0';
    runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                          "Storage.CopyAsync.end", 0, 0, E_FAIL);
    return E_FAIL;
  }

  const HANDLE destination = ::CreateFileW(
      g_resolved_path, GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (destination == INVALID_HANDLE_VALUE) {
    ::CloseHandle(source);
    g_resolved_path[0] = L'\0';
    runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                          "Storage.CopyAsync.end", 0, 0, E_FAIL);
    return E_FAIL;
  }

  const bool copied = CopyFileContents(source, destination);
  ::CloseHandle(destination);
  ::CloseHandle(source);

  if (!copied) {
    // Un destino a medio escribir es peor que ninguno: el juego lo abriria y
    // fallaria mucho mas adelante, ya sin contexto.
    ::DeleteFileW(g_resolved_path);
    g_resolved_path[0] = L'\0';
    runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                          "Storage.CopyAsync.end", 0, 0, E_FAIL);
    return E_FAIL;
  }

  g_file_operation_ready = true;
  log::Write("IStorageFile::CopyAsync copied import into LocalFolder");
  const HRESULT result = winrt::ReturnSingleton(&g_file_operation, operation);
  runtime_trace::Record(runtime_trace::BoundaryKind::Storage,
                        "Storage.CopyAsync.end",
                        reinterpret_cast<uintptr_t>(&g_file_operation), 0,
                        result);
  return result;
}

}  // namespace shim::storage
