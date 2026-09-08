#include "shim/file_picker.h"
#include "shim/winrt_string_compat.h"

#include <commdlg.h>

#include <winstring.h>

#include "shim/app_model.h"
#include "shim/app_window.h"
#include "shim/log.h"
#include "shim/storage.h"
#include "shim/winrt_vtable.h"

namespace shim::storage {

using winrt::Method;

namespace {

// Estado global, con los nombres del original entre parentesis.
wchar_t g_suggested_name[260] = {};               // word_62998C40
wchar_t g_default_extension[32] = {};             // word_62998E48
wchar_t g_picked_path[kMaxPathChars + 8] = {};    // String1 (0x62997C00)
bool g_pick_succeeded = false;                    // dword_62998E88

// Buffer de trabajo de los dialogos. El original lo declara en pila con holgura
// sobre kMaxPathChars; se replica el margen porque al nombre sugerido se le
// puede concatenar la extension sin volver a comprobar limites.
constexpr size_t kDialogBufferChars = 1048;

// OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT
constexpr DWORD kSaveFlags = 0x0008080A;

// OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
constexpr DWORD kOpenFlags = 0x00081808;

bool ContainsDot(const wchar_t* text) noexcept {
  for (const wchar_t* cursor = text; *cursor != L'\0'; ++cursor) {
    if (*cursor == L'.') {
      return true;
    }
  }
  return false;
}

void CopyString(wchar_t* destination, size_t capacity,
                const wchar_t* source) noexcept {
  if (source == nullptr) {
    destination[0] = L'\0';
    return;
  }
  ::lstrcpynW(destination, source, static_cast<int>(capacity));
}

// Deja el resultado del dialogo en el estado de modulo.
void RecordResult(bool succeeded, const wchar_t* path) noexcept {
  g_pick_succeeded = succeeded;
  if (succeeded) {
    ::lstrcpynW(g_picked_path, path, kMaxPathChars);
  } else {
    g_picked_path[0] = L'\0';
  }
}

// Parte comun de OPENFILENAMEW. lStructSize se fija a 88, el tamano de la
// estructura en x86 sin los campos de Windows 2000; usar sizeof() daria 88
// igualmente, pero el original lo codifica y conviene no depender del SDK.
void InitializeOpenFileName(OPENFILENAMEW* ofn, wchar_t* buffer,
                            DWORD flags) noexcept {
  ofn->lStructSize = 88;
  ofn->hwndOwner = app_window::Handle();
  ofn->lpstrFilter = kWorldFilter;
  ofn->lpstrFile = buffer;
  ofn->nMaxFile = kMaxPathChars;
  ofn->Flags = flags;
}

void SetSuggestedFileName(const wchar_t* name) noexcept {
  CopyString(g_suggested_name, CountOf(g_suggested_name), name);
}

void SetDefaultExtension(const wchar_t* extension) noexcept {
  CopyString(g_default_extension, CountOf(g_default_extension), extension);
}

bool ShowSaveDialog() noexcept {
  OPENFILENAMEW ofn = {};
  wchar_t buffer[kDialogBufferChars] = {};

  if (g_suggested_name[0] != L'\0') {
    ::lstrcpynW(buffer, g_suggested_name, kMaxPathChars);
  }

  // Si la sugerencia no trae extension se le pega .mcworld. El limite deja
  // sitio para los 8 caracteres de la extension mas el NUL.
  const int length = ::lstrlenW(buffer);
  if (length != 0 && length + 9 <= kMaxPathChars + 15 && !ContainsDot(buffer)) {
    ::lstrcatW(buffer, kWorldExtension);
  }

  InitializeOpenFileName(&ofn, buffer, kSaveFlags);

  // La extension por defecto va sin punto. Si el juego impuso una que ya empieza
  // por punto se usa tal cual; en otro caso se cae a "mcworld".
  const wchar_t* default_extension = kWorldExtensionNoDot;
  if (g_default_extension[0] != L'\0' && g_default_extension[0] != L'.') {
    default_extension = g_default_extension;
  }
  ofn.lpstrDefExt = default_extension;

  const bool confirmed = ::GetSaveFileNameW(&ofn) != FALSE;
  RecordResult(confirmed, buffer);

  if (confirmed) {
    // El fichero se crea vacio aqui mismo. El codigo UWP espera que
    // PickSaveFileAsync entregue un StorageFile ya existente, no una ruta a
    // secas, asi que sin esto la escritura posterior falla.
    const HANDLE file =
        ::CreateFileW(g_picked_path, GENERIC_WRITE,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
      ::CloseHandle(file);
    }
  }

  log::Write(confirmed ? "FileSavePicker redirected to Win32 save dialog"
                       : "FileSavePicker Win32 save dialog cancelled");
  return confirmed;
}

bool ShowOpenDialog() noexcept {
  OPENFILENAMEW ofn = {};
  wchar_t buffer[kDialogBufferChars] = {};

  InitializeOpenFileName(&ofn, buffer, kOpenFlags);
  ofn.lpstrDefExt = kWorldExtensionNoDot;

  const bool confirmed = ::GetOpenFileNameW(&ofn) != FALSE;
  RecordResult(confirmed, buffer);

  // RecordResult() ya actualiza el estado compartido de los dos pickers
  // (dword_62998E88). GetResults consulta ese mismo estado mediante
  // LastPickSucceeded(), igual que el binario original.

  log::Write(confirmed ? "FileOpenPicker redirected to Win32 open dialog"
                       : "FileOpenPicker Win32 open dialog cancelled");
  return confirmed;
}

}  // namespace

const wchar_t* LastPickedPath() noexcept {
  return g_picked_path;
}

bool LastPickSucceeded() noexcept {
  return g_pick_succeeded;
}

// ---------------------------------------------------------------------------
// Los selectores como objetos WinRT
//
// El juego configura el selector antes de mostrarlo: le pone una ubicacion
// sugerida, un nombre de fichero y una lista de extensiones. Casi nada de eso
// se puede trasladar a un dialogo de Win32, que ya tiene sus propias
// convenciones, asi que la mayoria de los ajustes se aceptan y se ignoran.
//
// Lo unico que se conserva es el nombre sugerido, porque es lo unico que el
// usuario nota.
// ---------------------------------------------------------------------------

namespace {

// Propiedad de cadena que siempre devuelve vacio (sub_6294DF30).
//
// Vacio y S_OK, no un error: el juego lee estas propiedades para rellenar su
// propia interfaz, y un fallo le haria abortar la operacion entera.
HRESULT SHIM_COM GetEmptyString(winrt::Object* self, HSTRING* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  return shim::winrt_string::Create(nullptr, 0, out);
}

// Ajuste que se acepta y se descarta (sub_6294DF50 y sub_6294DF80).
HRESULT SHIM_COM IgnoreSetting(winrt::Object* self, int value) noexcept {
  (void)self;
  (void)value;
  return S_OK;
}

// Propiedad numerica o de puntero que siempre devuelve cero (sub_6294DF60).
HRESULT SHIM_COM GetZero(winrt::Object* self, void** out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  return S_OK;
}

// put_DefaultFileExtension (0x6294DFE0). El original limita el texto a 31
// WCHAR mas NUL. No debe compartir estado con SuggestedFileName.
HRESULT SHIM_COM PutDefaultExtension(winrt::Object* self,
                                      HSTRING extension) noexcept {
  (void)self;
  UINT32 length = 0;
  const wchar_t* text = extension != nullptr
                            ? shim::winrt_string::GetRawBuffer(extension, &length)
                            : nullptr;
  SetDefaultExtension(text != nullptr && length != 0 ? text : L"");
  return S_OK;
}

// put_SuggestedFileName (0x6294E050).
HRESULT SHIM_COM PutSuggestedFileName(winrt::Object* self,
                                      HSTRING name) noexcept {
  (void)self;
  UINT32 length = 0;
  const wchar_t* text =
      name != nullptr ? shim::winrt_string::GetRawBuffer(name, &length) : nullptr;
  SetSuggestedFileName(text != nullptr && length != 0 ? text : L"");
  return S_OK;
}

// Colecciones de configuracion de los pickers.
//
// IMPORTANTE: son DOS interfaces distintas y sus vtables NO tienen solo las
// seis entradas de IInspectable. El binario original usa:
//
//   FileTypeChoices (kind 75) @ 0x62993B58 : 12 entradas
//   FileTypeFilter  (kind 76) @ 0x62993B88 : 18 entradas
//
// La reconstruccion anterior compartia una vtable truncada de 6 entradas. Al
// configurar el picker, el juego llama Insert/Append por indice y saltaba fuera
// del array. En una build MSVC concreta ese puntero fuera de rango terminaba en
// QueryInterface, que interpretaba los argumentos de Insert/Append como
// (this, IID, out) y escribia a una direccion basura.
//
// Solo conservamos estado suficiente para que el juego configure el dialogo:
// el filtro Win32 real sigue siendo kWorldFilter. Las firmas y el numero de
// slots se mantienen porque forman parte del ABI.

HRESULT SHIM_COM FileTypeChoicesInsert(winrt::Object* self, HSTRING key,
                                        void* value, BYTE* replaced) noexcept {
  (void)self;
  (void)key;
  (void)value;
  if (replaced != nullptr) {
    *replaced = 0;
  }
  return S_OK;
}

Method g_file_type_choices_vtable[] = {
    reinterpret_cast<Method>(&winrt::QueryInterface),         //  0
    reinterpret_cast<Method>(&winrt::AddRef),                 //  1
    reinterpret_cast<Method>(&winrt::Release),                //  2
    reinterpret_cast<Method>(&winrt::GetIids),                //  3
    reinterpret_cast<Method>(&winrt::GetRuntimeClassName),    //  4
    reinterpret_cast<Method>(&winrt::GetTrustLevel),          //  5
    reinterpret_cast<Method>(&winrt::NotImplementedNoCleanup), //  6
    reinterpret_cast<Method>(&winrt::NotImplementedNoCleanup), //  7
    reinterpret_cast<Method>(&winrt::NotImplementedNoCleanup), //  8
    reinterpret_cast<Method>(&winrt::NotImplementedNoCleanup), //  9
    reinterpret_cast<Method>(&FileTypeChoicesInsert),         // 10
    reinterpret_cast<Method>(&winrt::NotImplementedNoCleanup), // 11
};
static_assert(CountOf(g_file_type_choices_vtable) == 12,
              "FileTypeChoices vtable must match 0x62993B58");

// 0x6294E400. Coleccion vacia: cualquier GetAt esta fuera de rango.
HRESULT SHIM_COM FileTypeFilterGetAt(winrt::Object* self, UINT32 index,
                                      HSTRING* out) noexcept {
  (void)self;
  (void)index;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = nullptr;
  return static_cast<HRESULT>(0x8000000B);  // E_BOUNDS
}

// 0x6294E420.
HRESULT SHIM_COM FileTypeFilterGetSize(winrt::Object* self,
                                        UINT32* out) noexcept {
  (void)self;
  if (out == nullptr) {
    return E_POINTER;
  }
  *out = 0;
  return S_OK;
}

// 0x6294E440. El original devuelve el mismo singleton como vista.
HRESULT SHIM_COM FileTypeFilterGetView(winrt::Object* self,
                                        void** out) noexcept {
  return winrt::ReturnSingleton(self, out);
}

// 0x6294E470.
HRESULT SHIM_COM FileTypeFilterIndexOf(winrt::Object* self, HSTRING value,
                                        UINT32* index, BYTE* found) noexcept {
  (void)self;
  (void)value;
  if (index != nullptr) {
    *index = 0;
  }
  if (found != nullptr) {
    *found = 0;
  }
  return S_OK;
}

HRESULT SHIM_COM FileTypeFilterSetAt(winrt::Object* self, UINT32 index,
                                      HSTRING value) noexcept {
  (void)self;
  (void)index;
  (void)value;
  return S_OK;
}

HRESULT SHIM_COM FileTypeFilterInsertAt(winrt::Object* self, UINT32 index,
                                         HSTRING value) noexcept {
  (void)self;
  (void)index;
  (void)value;
  return S_OK;
}

HRESULT SHIM_COM FileTypeFilterRemoveAt(winrt::Object* self,
                                         UINT32 index) noexcept {
  (void)self;
  (void)index;
  return S_OK;
}

// 0x6294E4C0. Es el slot que usa el juego al anadir ".mcworld".
HRESULT SHIM_COM FileTypeFilterAppend(winrt::Object* self,
                                       HSTRING value) noexcept {
  (void)self;
  (void)value;
  return S_OK;
}

// 0x6294E4D0.
HRESULT SHIM_COM FileTypeFilterNoArg(winrt::Object* self) noexcept {
  (void)self;
  return S_OK;
}

Method g_file_type_filter_vtable[] = {
    reinterpret_cast<Method>(&winrt::QueryInterface),       //  0
    reinterpret_cast<Method>(&winrt::AddRef),               //  1
    reinterpret_cast<Method>(&winrt::Release),              //  2
    reinterpret_cast<Method>(&winrt::GetIids),              //  3
    reinterpret_cast<Method>(&winrt::GetRuntimeClassName),  //  4
    reinterpret_cast<Method>(&winrt::GetTrustLevel),        //  5
    reinterpret_cast<Method>(&FileTypeFilterGetAt),         //  6  E400
    reinterpret_cast<Method>(&FileTypeFilterGetSize),       //  7  E420
    reinterpret_cast<Method>(&FileTypeFilterGetView),       //  8  E440
    reinterpret_cast<Method>(&FileTypeFilterIndexOf),       //  9  E470
    reinterpret_cast<Method>(&FileTypeFilterSetAt),         // 10  E490
    reinterpret_cast<Method>(&FileTypeFilterInsertAt),      // 11  E4A0
    reinterpret_cast<Method>(&FileTypeFilterRemoveAt),      // 12  E4B0
    reinterpret_cast<Method>(&FileTypeFilterAppend),        // 13  E4C0
    reinterpret_cast<Method>(&FileTypeFilterNoArg),         // 14  E4D0
    reinterpret_cast<Method>(&FileTypeFilterNoArg),         // 15  E4D0
    reinterpret_cast<Method>(&FileTypeFilterAppend),        // 16  E4C0
    reinterpret_cast<Method>(&FileTypeFilterNoArg),         // 17  E4D0
};
static_assert(CountOf(g_file_type_filter_vtable) == 18,
              "FileTypeFilter vtable must match 0x62993B88");

winrt::Object g_file_type_choices = {g_file_type_choices_vtable, 1, 75};
winrt::Object g_file_type_filter = {g_file_type_filter_vtable, 1, 76};

HRESULT SHIM_COM GetFileTypeChoices(winrt::Object* self, void** out) noexcept {
  (void)self;
  return winrt::ReturnSingleton(&g_file_type_choices, out);
}

HRESULT SHIM_COM GetFileTypeFilter(winrt::Object* self, void** out) noexcept {
  (void)self;
  return winrt::ReturnSingleton(&g_file_type_filter, out);
}

// PickSaveFileAsync (sub_6294E090): muestra el dialogo AHORA, de forma
// sincrona, y devuelve una operacion ya terminada.
//
// Es una mentira deliberada y la unica que funciona. Un dialogo modal de Win32
// bombea mensajes en el hilo que lo abre; hacerlo de verdad asincrono obligaria
// a un hilo aparte, y GetOpenFileName no es seguro fuera del hilo de interfaz.
HRESULT SHIM_COM PickSaveFileAsync(winrt::Object* self,
                                   void** operation) noexcept {
  (void)self;
  if (operation == nullptr) {
    return E_POINTER;
  }
  ShowSaveDialog();
  return winrt::ReturnSingleton(ExportOperation(), operation);
}

// PickSingleFileAsync (sub_6294E2C0)
HRESULT SHIM_COM PickSingleFileAsync(winrt::Object* self,
                                     void** operation) noexcept {
  (void)self;
  if (operation == nullptr) {
    return E_POINTER;
  }
  ShowOpenDialog();
  return winrt::ReturnSingleton(ImportOperation(), operation);
}

#define SHIM_PICKER_IINSPECTABLE                          \
  reinterpret_cast<Method>(&winrt::QueryInterface),       \
      reinterpret_cast<Method>(&winrt::AddRef),           \
      reinterpret_cast<Method>(&winrt::Release),          \
      reinterpret_cast<Method>(&winrt::GetIids),          \
      reinterpret_cast<Method>(&winrt::GetRuntimeClassName), \
      reinterpret_cast<Method>(&winrt::GetTrustLevel)

// IFileSavePicker, contrastada con la vtable del binario en 0x62993AC4:
// 20 entradas.
Method g_save_picker_vtable[] = {
    SHIM_PICKER_IINSPECTABLE,
    reinterpret_cast<Method>(&GetEmptyString),        //  6
    reinterpret_cast<Method>(&IgnoreSetting),         //  7
    reinterpret_cast<Method>(&GetZero),               //  8
    reinterpret_cast<Method>(&IgnoreSetting),         //  9
    reinterpret_cast<Method>(&GetEmptyString),        // 10
    reinterpret_cast<Method>(&IgnoreSetting),         // 11
    reinterpret_cast<Method>(&GetFileTypeChoices),    // 12
    reinterpret_cast<Method>(&GetEmptyString),        // 13
    reinterpret_cast<Method>(&PutDefaultExtension),   // 14
    reinterpret_cast<Method>(&winrt::NotImplementedNoCleanup), // 15
    reinterpret_cast<Method>(&winrt::NotImplementedNoCleanup), // 16
    reinterpret_cast<Method>(&GetEmptyString),        // 17
    reinterpret_cast<Method>(&PutSuggestedFileName),  // 18
    reinterpret_cast<Method>(&PickSaveFileAsync),     // 19
};

// IFileOpenPicker, contrastada con la vtable del binario en 0x62993B14:
// 17 entradas.
Method g_open_picker_vtable[] = {
    SHIM_PICKER_IINSPECTABLE,
    reinterpret_cast<Method>(&GetZero),               //  6
    reinterpret_cast<Method>(&IgnoreSetting),         //  7
    reinterpret_cast<Method>(&GetEmptyString),        //  8
    reinterpret_cast<Method>(&IgnoreSetting),         //  9
    reinterpret_cast<Method>(&GetZero),               // 10
    reinterpret_cast<Method>(&IgnoreSetting),         // 11
    reinterpret_cast<Method>(&GetEmptyString),        // 12
    reinterpret_cast<Method>(&IgnoreSetting),         // 13
    reinterpret_cast<Method>(&GetFileTypeFilter),     // 14
    reinterpret_cast<Method>(&PickSingleFileAsync),   // 15
    reinterpret_cast<Method>(&winrt::NotImplementedNoCleanup), // 16
};

#undef SHIM_PICKER_IINSPECTABLE

Method g_picker_factory_vtable[] = {
    reinterpret_cast<Method>(&winrt::QueryInterface),
    reinterpret_cast<Method>(&winrt::AddRef),
    reinterpret_cast<Method>(&winrt::Release),
    reinterpret_cast<Method>(&winrt::GetIids),
    reinterpret_cast<Method>(&winrt::GetRuntimeClassName),
    reinterpret_cast<Method>(&winrt::GetTrustLevel),
    reinterpret_cast<Method>(&winrt::ActivateInstance),
};

winrt::Object g_save_picker_factory = {g_picker_factory_vtable, 1,
                                       kSavePickerFactoryKind};
winrt::Object g_open_picker_factory = {g_picker_factory_vtable, 1,
                                       kOpenPickerFactoryKind};
winrt::Object g_save_picker = {g_save_picker_vtable, 1, kSavePickerKind};
winrt::Object g_open_picker = {g_open_picker_vtable, 1, kOpenPickerKind};

}  // namespace

winrt::Object* SavePickerFactory() noexcept {
  return &g_save_picker_factory;
}

winrt::Object* OpenPickerFactory() noexcept {
  return &g_open_picker_factory;
}

winrt::Object* PickerInstance(int factory_kind) noexcept {
  if (factory_kind == kSavePickerFactoryKind) {
    return &g_save_picker;
  }
  if (factory_kind == kOpenPickerFactoryKind) {
    return &g_open_picker;
  }
  return nullptr;
}

}  // namespace shim::storage
