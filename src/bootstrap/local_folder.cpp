#include "shim/local_folder.h"

#include <shlobj.h>

#include "shim/branding.h"
#include "shim/log.h"
#include "shim/runtime_config.h"
#include "shim/storage.h"

namespace shim::local_folder {
namespace {

wchar_t g_install_directory[MAX_PATH] = {};

// Carpeta que contiene al ejecutable del juego.
//
// Es lo que devuelve Package.InstalledLocation, y de ahi cuelga TODO el arbol de
// assets. El original la resolvia con GetCurrentDirectoryW (importa esa API y no
// usa GetModuleFileName), lo cual funciona solo mientras el directorio de
// trabajo coincida con el del juego. Lanzandolo desde otro sitio:
//
//     D:\> "D:\MC\...\Minecraft.Windows.exe"
//
// se le contestaba "D:\", el juego no encontraba un solo asset y moria de
// access violation en game+0x7E4E11 desreferenciando el nulo, al segundo de
// arrancar.
//
// El modulo es la fuente correcta y ademas inmutable: no depende del CWD ni de
// que el juego lo cambie a mitad de carga, que era el motivo por el que habia
// que capturarlo temprano. En el caso normal el valor es identico al de antes.
bool ResolveInstallDirectory() noexcept {
  const DWORD length =
      ::GetModuleFileNameW(nullptr, g_install_directory, MAX_PATH);
  // Devuelve el tamano del buffer exacto cuando la ruta no cabe, y en ese caso
  // el contenido esta truncado: recortarlo daria una carpeta que no existe.
  if (length != 0 && length < MAX_PATH) {
    for (DWORD i = length; i > 0; --i) {
      if (g_install_directory[i - 1] == L'\\') {
        // Se corta DESPUES de la barra solo si con eso no queda una raiz sola
        // ("C:\"), donde la barra final si forma parte de la ruta.
        g_install_directory[i - 1 == 2 ? i : i - 1] = L'\0';
        return true;
      }
    }
  }

  // Respaldo: el comportamiento del original. Peor, pero no peor que antes.
  if (::GetCurrentDirectoryW(MAX_PATH, g_install_directory) != 0) {
    log::Write("install directory fell back to the current directory");
    return true;
  }

  // Un proceso siempre tiene directorio actual, pero si la llamada falla el
  // punto es un sustituto valido para resolver rutas relativas.
  ::lstrcpyW(g_install_directory, L".");
  return false;
}

// Contenido con el que se siembra options.txt cuando no existe o esta vacio.
//
// El juego arranca sin el fichero, pero entonces genera sus propios valores por
// defecto pensados para movil: joystick tactil, sensibilidad de pantalla y
// distancia de render corta. Sembrarlo con valores de escritorio es la
// diferencia entre un primer arranque jugable y uno que parece roto.
//
// Las ramas modernas usan old_game_version=0.15.10. 0.13.2 no comparte ese
// detalle: su bootstrap escribe 0.13.2 y por eso tiene un seed separado mas
// abajo. El campo solo sirve para decidir migraciones del primer arranque.
constexpr char kDefaultOptions[] =
    "mp_username:Steve\r\n"
    "game_difficulty_new:1\r\n"
    "game_thirdperson:0\r\n"
    "gfx_dpadscale:0.5\r\n"
    "mp_server_visible:1\r\n"
    "mp_xboxlive_visible:0\r\n"
    "game_flatworldlayers:[7,3,3,2]\r\n"
    "game_limitworldsize:0\r\n"
    "game_language:en_US\r\n"
    "game_skintypefull:Standard_Steve\r\n"
    "game_lastcustomskinnew:\r\n"
    "ctrl_sensitivity:0.33\r\n"
    "ctrl_invertmouse:0\r\n"
    "ctrl_islefthanded:0\r\n"
    // Raton y teclado, no pantalla tactil: es lo que cambia el juego de
    // injugable a jugable en un escritorio.
    "ctrl_usetouchscreen:0\r\n"
    "ctrl_usetouchjoypad:0\r\n"
    "ctrl_swapjumpandsneak:0\r\n"
    "feedback_vibration:1\r\n"
    "ctrl_autojump:0\r\n"
    "ctrl_keyboardlayout:0\r\n"
    "ctrl_gamePadMap:[0,1,t:1,t:0,2,3,10,11,8,9,13,4,5,6,12,X,X,X,X]\r\n"
    "gfx_renderdistance_new:96\r\n"
    "gfx_viewbobbing:1\r\n"
    "gfx_fancygraphics:1\r\n"
    "gfx_fancyskies:1\r\n"
    "gfx_animatetextures:1\r\n"
    "gfx_hidegui:0\r\n"
    "gfx_field_of_view:70\r\n"
    "gfx_gamma:0\r\n"
    "gfx_fullscreen:0\r\n"
    "audio_sound:1\r\n"
    "audio_music:1\r\n"
    "dev_autoloadlevel:0\r\n"
    "dev_showchunkmap:0\r\n"
    "dev_disablefilesystem:0\r\n"
    "old_game_version_major:0\r\n"
    "old_game_version_minor:15\r\n"
    "old_game_version_patch:10\r\n"
    "old_game_version_beta:0\r\n";

// 0.13.2 lleva el mismo seed de escritorio salvo los campos de version. El
// bootstrap original lo escribe como un unico bloque de exactamente 0x382
// bytes; mantener una constante separada evita que 0.15.10 contamine la ruta
// the 0.13.2 path and also makes that length verifiable.
constexpr char kDefaultOptions0132[] =
    "mp_username:Steve\r\n"
    "game_difficulty_new:1\r\n"
    "game_thirdperson:0\r\n"
    "gfx_dpadscale:0.5\r\n"
    "mp_server_visible:1\r\n"
    "mp_xboxlive_visible:0\r\n"
    "game_flatworldlayers:[7,3,3,2]\r\n"
    "game_limitworldsize:0\r\n"
    "game_language:en_US\r\n"
    "game_skintypefull:Standard_Steve\r\n"
    "game_lastcustomskinnew:\r\n"
    "ctrl_sensitivity:0.33\r\n"
    "ctrl_invertmouse:0\r\n"
    "ctrl_islefthanded:0\r\n"
    "ctrl_usetouchscreen:0\r\n"
    "ctrl_usetouchjoypad:0\r\n"
    "ctrl_swapjumpandsneak:0\r\n"
    "feedback_vibration:1\r\n"
    "ctrl_autojump:0\r\n"
    "ctrl_keyboardlayout:0\r\n"
    "ctrl_gamePadMap:[0,1,t:1,t:0,2,3,10,11,8,9,13,4,5,6,12,X,X,X,X]\r\n"
    "gfx_renderdistance_new:96\r\n"
    "gfx_viewbobbing:1\r\n"
    "gfx_fancygraphics:1\r\n"
    "gfx_fancyskies:1\r\n"
    "gfx_animatetextures:1\r\n"
    "gfx_hidegui:0\r\n"
    "gfx_field_of_view:70\r\n"
    "gfx_gamma:0\r\n"
    "gfx_fullscreen:0\r\n"
    "audio_sound:1\r\n"
    "audio_music:1\r\n"
    "dev_autoloadlevel:0\r\n"
    "dev_showchunkmap:0\r\n"
    "dev_disablefilesystem:0\r\n"
    "old_game_version_major:0\r\n"
    "old_game_version_minor:13\r\n"
    "old_game_version_patch:2\r\n"
    "old_game_version_beta:0\r\n";

constexpr DWORD kDefaultOptionsLength =
    static_cast<DWORD>(sizeof(kDefaultOptions) - 1);
constexpr DWORD kDefaultOptions0132Length =
    static_cast<DWORD>(sizeof(kDefaultOptions0132) - 1);
static_assert(kDefaultOptions0132Length == 0x382,
              "0.13.2 options seed must match the original 0x382-byte block");

// Escribe el contenido por defecto y cierra el fichero.
void WriteDefaults(HANDLE file, const char* contents, DWORD length) noexcept {
  DWORD written = 0;
  ::WriteFile(file, contents, length, &written, nullptr);
  ::CloseHandle(file);
}

// True si el fichero existe pero no tiene contenido.
//
// Un options.txt de cero bytes es peor que uno inexistente: el juego lo abre,
// no lee nada y se queda con sus valores de movil sin volver a intentarlo. Pasa
// cuando un arranque anterior se corto entre crear el fichero y escribirlo.
bool IsEmptyFile(const wchar_t* path) noexcept {
  WIN32_FILE_ATTRIBUTE_DATA attributes = {};
  if (!::GetFileAttributesExW(path, GetFileExInfoStandard, &attributes)) {
    // Si ni siquiera se pueden leer los atributos, se trata como vacio y se
    // regenera: es el camino que deja el fichero en un estado conocido.
    return true;
  }
  return attributes.nFileSizeHigh == 0 && attributes.nFileSizeLow == 0;
}

// Anade un componente a la ruta y crea el directorio resultante.
void AppendAndCreate(wchar_t* path, const wchar_t* component) noexcept {
  ::lstrcatW(path, component);
  // Se ignora el error: lo normal en el segundo arranque es que ya exista, y
  // distinguir "ya existe" de "no se pudo" no cambia lo que hacemos despues.
  ::CreateDirectoryW(path, nullptr);
}

bool PrepareWithDefaults(const char* defaults, DWORD defaults_length) noexcept {
  ResolveInstallDirectory();
  log::Writef("install directory: '%ls'", g_install_directory);

  // El juego resuelve sus assets con rutas RELATIVAS, asi que da por hecho que
  // el directorio de trabajo es el suyo. Es la misma suposicion que hacia el
  // original al usar GetCurrentDirectoryW como carpeta de instalacion, solo que
  // sin comprobarla: lanzando el ejecutable desde cualquier otro sitio
  //
  //     D:\> "D:\MC\...\Minecraft.Windows.exe"
  //
  // no encontraba un solo fichero y moria de access violation en el
  // constructor de AppMain, antes incluso del primer fopen.
  //
  // Fijarlo aqui arregla de una vez a TODOS los consumidores de rutas
  // relativas, en vez de ir persiguiendolos uno a uno. Cuando se lanza desde la
  // carpeta del juego, que es el caso normal, esto no cambia nada.
  if (::SetCurrentDirectoryW(g_install_directory)) {
    log::Write("current directory set to the install directory");
  } else {
    log::Write("SetCurrentDirectoryW(install directory) failed");
  }

  runtime_config::Initialize(g_install_directory);

  wchar_t base[MAX_PATH] = {};
  if (runtime_config::Get().local_save_path) {
    ::lstrcpyW(base, g_install_directory);
  } else {
    if (::SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr,
                           SHGFP_TYPE_CURRENT, base) != S_OK) {
      log::Write("SHGetFolderPathW(CSIDL_APPDATA) failed");
      return false;
    }
  }

  const int base_length = ::lstrlenW(base);
  const int display_name_length = ::lstrlenW(SHIM_DISPLAY_NAME_W);
  if (base_length + 1 + display_name_length >= MAX_PATH) {
    log::Write("local save root path is too long");
    return false;
  }

  ::lstrcatW(base, L"\\");
  ::lstrcatW(base, SHIM_DISPLAY_NAME_W);

  // El modulo de almacenamiento resuelve contra esto todas las rutas relativas
  // que le lleguen por WinRT, asi que se fija antes de crear nada: si la
  // creacion falla, al menos las rutas apuntan al sitio correcto.
  storage::SetLocalFolder(base);
  ::CreateDirectoryW(base, nullptr);

  wchar_t options[MAX_PATH] = {};
  ::lstrcpyW(options, base);
  AppendAndCreate(options, L"\\games");
  AppendAndCreate(options, L"\\com.mojang");
  AppendAndCreate(options, L"\\minecraftpe");
  ::lstrcatW(options, L"\\options.txt");

  // CREATE_NEW, no CREATE_ALWAYS: si el jugador ya tiene sus ajustes, no se
  // tocan. El fallo por "ya existe" es el caso normal a partir del segundo
  // arranque y se distingue del resto por el codigo de error.
  HANDLE file = ::CreateFileW(options, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    WriteDefaults(file, defaults, defaults_length);
    log::Write(
        "created local save games/com.mojang/minecraftpe/options.txt");
    return true;
  }

  if (::GetLastError() != ERROR_FILE_EXISTS) {
    log::Write("CreateFileW(options.txt) failed");
    return true;
  }

  if (!IsEmptyFile(options)) {
    log::Write("local save options.txt already exists");
    return true;
  }

  file = ::CreateFileW(options, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    log::Write("could not regenerate empty options.txt");
    return true;
  }

  log::Write("regenerating empty local save options.txt");
  WriteDefaults(file, defaults, defaults_length);
  return true;
}

}  // namespace

const wchar_t* InstallDirectory() noexcept {
  return g_install_directory;
}

bool Prepare() noexcept {
  return PrepareWithDefaults(kDefaultOptions, kDefaultOptionsLength);
}

bool Prepare0132() noexcept {
  return PrepareWithDefaults(kDefaultOptions0132, kDefaultOptions0132Length);
}

}  // namespace shim::local_folder
