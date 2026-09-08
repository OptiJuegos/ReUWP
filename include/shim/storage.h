// StorageFile y StorageFolder falsos sobre el sistema de ficheros de Win32.
//
// Original: sub_6294AFB0 (union de rutas), sub_6294AE80 (CreateFileAsync),
// sub_6294E5C0 (CopyAsync) y sub_6294AF70 (get_Path).
//
// En UWP la app solo escribe dentro de su LocalFolder. El shim reproduce esa
// idea con un directorio base bajo %APPDATA%, y toda ruta relativa que llegue
// por WinRT se resuelve dentro de el.

#pragma once

#include "shim/winrt_object.h"

namespace shim::storage {

using winrt::Object;

// Capacidad del buffer de ruta resuelta (dword_62998420 en el original).
//
// Muy por encima de MAX_PATH porque los nombres de mundo pueden ser largos. El
// limite efectivo que impone la comprobacion de la union de rutas es 1038
// caracteres entre base y relativa, dejando sitio para el separador y el NUL.
inline constexpr size_t kPathCapacity = 1040;
inline constexpr size_t kMaxJoinedLength = 1038;

// Tamano de bloque de CopyAsync. El original usa 2 KB en pila.
inline constexpr size_t kCopyBlockSize = 2048;

// Etiquetas de tipo, leidas del binario (tabla de singletons de Win32Bootstrap
// y switch de sub_6294C180).
enum Kind : int {
  // Los dos StorageFolder del shim. Comparten vtable y se distinguen SOLO por
  // este campo, que es lo que lee get_Path para saber que ruta devolver.
  kInstalledLocationKind = 23,  // donde vive el EXE
  kLocalFolderKind = 24,        // donde el juego guarda sus datos

  // Los selectores siguen el patron fabrica -> instancia como el resto del
  // modelo de aplicacion: el juego pide la clase, recibe una fabrica, y de ella
  // saca el selector sobre el que configura filtros antes de mostrarlo.
  kSavePickerFactoryKind = 71,
  kSavePickerKind = 72,
  kOpenPickerFactoryKind = 73,
  kOpenPickerKind = 74,

  kPickerOperationKind = 77,  // IAsyncOperation<StorageFile>
  kStorageFileKind = 78,      // IStorageFile projection
  kStorageItemKind = 79,      // IStorageItem projection del mismo fichero
};

// Fabricas de los dos selectores, para la tabla de activacion.
Object* SavePickerFactory() noexcept;
Object* OpenPickerFactory() noexcept;

// Instancia que produce una de esas fabricas.
//
// Devuelve nullptr si el `kind` no es el de ninguna de las dos, para que el
// ActivateInstance compartido siga con su caso por defecto.
Object* PickerInstance(int factory_kind) noexcept;

// Los dos StorageFolder.
//
// InstalledLocation es lo que devuelve Package: el directorio del EXE, de donde
// el juego lee sus recursos. LocalFolder es lo que devuelven las tres carpetas
// de ApplicationData: donde escribe.
Object* InstalledLocationFolder() noexcept;
Object* LocalFolderObject() noexcept;

// IStorageItem::get_Path (sub_6294AF70)
//
// Compartido por los dos StorageFolder. Devuelve el directorio de arranque si
// el objeto es el InstalledLocation, y el LocalFolder en cualquier otro caso.
HRESULT SHIM_COM GetFolderPath(Object* self, HSTRING* out) noexcept;

// Las dos operaciones asincronas de seleccion de fichero.
//
// Son objetos distintos por identidad, como en el original. Ambos pickers usan
// el mismo estado del ultimo dialogo (dword_62998E88); CreateFileAsync/CopyAsync
// usan internamente una tercera operacion con su propio estado (byte_62998E8C).
Object* ImportOperation() noexcept;
Object* ExportOperation() noexcept;

// Resultado de una operacion `kind` 77 de GetResults.
//
// Para Save/OpenPicker devuelve el StorageFile del ultimo dialogo si este tuvo
// exito. Para la operacion interna de CreateFileAsync/CopyAsync devuelve el
// StorageFile ligado a la ruta resuelta si esa operacion termino bien.
// Null NO es un error: representa cancelacion o una operacion sin resultado.
Object* PickerResult(const Object* operation) noexcept;

// Fija el directorio base que hace de LocalFolder (word_62997368). Lo establece
// el bootstrap tras crear %APPDATA%\MinecraftPE.
void SetLocalFolder(const wchar_t* path) noexcept;

// Directorio base actual.
const wchar_t* LocalFolder() noexcept;

// Une una ruta relativa sobre el directorio base y la deja en el buffer de ruta
// resuelta (sub_6294AFB0).
//
// Inserta un separador si la base no acaba en '\' o '/'. Devuelve false si la
// ruta resultante excediera kMaxJoinedLength, en cuyo caso el buffer no se toca.
bool ResolveInLocalFolder(HSTRING relative) noexcept;

// Ruta de origen de las importaciones: lo que eligio el usuario en el ultimo
// dialogo de apertura.
const wchar_t* LastPickedSourcePath() noexcept;

// IStorageItem::get_FileType (sub_6294E530)
//
// Devuelve la extension CON el punto, tal cual la espera WinRT (".mcworld").
// Sale de la ruta que corresponda al objeto: la del dialogo de apertura para el
// fichero de importacion, la ruta resuelta para el de exportacion.
HRESULT SHIM_COM GetFileType(Object* self, HSTRING* out) noexcept;

// IStorageFile::get_ContentType (sub_6294E5A0)
//
// Siempre "application/octet-stream". Un .mcworld es un ZIP y no tiene tipo MIME
// registrado; el juego solo comprueba que haya algo.
HRESULT SHIM_COM GetContentType(Object* self, HSTRING* out) noexcept;

// IStorageFolder::CreateFileAsync (sub_6294AE80)
//
// Crea el fichero de verdad, no solo la ruta: el codigo UWP espera recibir un
// StorageFile ya existente sobre el que escribir.
HRESULT SHIM_COM CreateFileAsync(Object* self, HSTRING name, int collision_option,
                                 void** operation) noexcept;

// IStorageFile::CopyAsync (sub_6294E5C0)
//
// Copia el fichero de origen dentro del LocalFolder. El origen es la ruta que
// dejo el ultimo dialogo de apertura, salvo que el objeto sea uno de los que ya
// apuntan al buffer resuelto.
HRESULT SHIM_COM CopyAsync(Object* self, void* destination_folder, HSTRING name,
                           int collision_option, void** operation) noexcept;

}  // namespace shim::storage
