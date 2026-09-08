// ABI layout definitions used by the game-version profiles.
//
// Per-build addresses do not live in this header. Each supported executable
// owns its concrete Layout or Version0132Layout in src/versions/v*/, while
// common code keeps using the stable accessors declared below. This keeps
// version data separate from bootstrap, input, FMOD, Direct3D and WinRT logic.
//
// All address fields are RVAs in bytes from the game module base. Object
// *_offset fields are byte offsets inside game-owned objects. A zero RVA means
// that the operation is not available for that profile.

#pragma once

#include "shim/common.h"

namespace shim::game {

// Version del EXE que nos ha cargado.
//
// Se distinguen por SizeOfImage de la cabecera PE, no por el nombre del fichero
// ni por recursos: es lo unico que el usuario no puede cambiar sin romper el
// binario, y se lee sin tocar disco.
enum class Version {
  kUnknown,
  kV0_13_2,
  kV0_15_10,
  kV1_1_5,
};


// Codigos de retorno de Win32Bootstrap. El juego no los mira, pero el original
// los distingue y conviene no perderlos: son la unica pista de por que no
// arranco cuando no hay depurador conectado.
enum class BootstrapResult : unsigned int {
  kOk = 0,
  kRegisterClassFailed = 1,
  kCreateWindowFailed = 2,
  kUnsupportedVersion = 3,
  kCompatibilityPatchFailed = 4,
};

// Addresses and object offsets used by the modern game-version profiles.
// Zero RVAs mean that the operation is not available for that profile.
struct PatchLayout {
  DWORD iat_cxx_throw_exception;
  DWORD iat_create_file2;
  DWORD iat_activation_factory;
  DWORD iat_d3d_compile;
  DWORD iat_d3d11_create_device;
  DWORD iat_vccorlib[5];
  DWORD iat_xbox_tcui[2];

  DWORD iid_d3d11_device2;
  DWORD iid_d3d11_context2;
  DWORD iid_dxgi_device3;
};

struct InputLayout {
  DWORD fn_make_mouse_event;
  DWORD fn_dispatch_input_event;
  DWORD fn_make_key_event;
  DWORD fn_translate_key;
  DWORD fn_make_text_event;
  DWORD fn_key_down;
  static constexpr DWORD kKeyUpDelta = 256;

  DWORD app_main_to_client;
  DWORD client_to_input;
  DWORD input_to_event_sink;
  DWORD input_to_cursor_mode;
  DWORD input_to_cursor_command;
  DWORD input_to_key_states;
  DWORD input_to_text_sequence;
};

struct RuntimeLayout {
  DWORD fn_make_app_main;
  DWORD fn_app_main_frame;
  DWORD fn_platform_frame_entry;
  DWORD fn_app_main_resize;
};

struct D3DLayout {
  DWORD fn_make_device_resources;
  DWORD device_resources_slot;
  DWORD fn_init_render_targets;

  DWORD owner_to_renderer;
  DWORD owner_to_device;
  DWORD renderer_to_context;
  DWORD renderer_to_swap_chain;
  DWORD renderer_to_render_target;
  DWORD renderer_to_depth_target;
  DWORD renderer_to_back_buffer;

  DWORD resource_owner_size;
  DWORD fn_alloc_resource_owner;
  DWORD fn_construct_resource_owner;
  DWORD resource_owner_slot;
  DWORD fn_init_d3d_device;
  DWORD fn_release_render_targets;
};

struct FmodLayout {
  DWORD fmod_delay_descriptor;
  DWORD fmod_expected_iat;
  DWORD fmod_expected_int;
  DWORD fmod_import_count;

  DWORD fmod_dll_name;
  DWORD fmod_delay_iat;
  DWORD fmod_bridge_slots;
};

// The aggregate remains profile-owned immutable data. Modern subsystems receive
// the individual sections from bootstrap instead of querying CurrentLayout().
struct Layout {
  PatchLayout patches;
  InputLayout input;
  RuntimeLayout runtime;
  D3DLayout d3d;
  FmodLayout fmod;
};

// Mapa de la rama separada de Minecraft 0.13.2.
//
// El bootstrap de esta version NO es una variante parametrizada del de 0.15.10
// y 1.1.5. El binario salta a una funcion distinta (FUN_629557e0) que crea un
// owner de recursos de 0x1c bytes, inicializa D3D sobre ese owner y solo despues
// construye DX::DeviceResources y MCPE_Host::AppMain. Por eso sus offsets viven
// en una estructura propia: meterlos a la fuerza en Layout haria que campos con
// el mismo nombre aparentasen tener la misma ABI cuando no la tienen.
//
// Todos los valores de esta estructura son RVAs EN BYTES desde la base del EXE,
// salvo los campos *_offset, que son desplazamientos dentro de objetos. Se han
// contrastado contra d3dcraft_dissassembly.txt, no solo contra pseudocodigo.
struct Version0132Layout {
  // IAT / datos de compatibilidad.
  DWORD iat_activation_factory;
  DWORD iat_d3d_compile;
  DWORD iat_d3d11_create_device;
  DWORD iat_frame_sleep;
  DWORD iid_d3d11_device2;
  DWORD iid_d3d11_context2;

  // Audio. 0.13.2 sustituye directamente 25 huecos consecutivos de la IAT
  // retardada, igual que la ruta vieja de 0.15 pero con otra tabla.
  DWORD fmod_dll_name;
  DWORD fmod_delay_iat;
  DWORD fmod_bridge_slots;

  // Parches Win32 propios de 0.13.2. Los tres file_override_* son, en orden,
  // _wfopen_s, _Fiopen(char*, openmode, prot) y _Fiopen(wchar_t*, openmode,
  // prot). Se conservan como RVAs documentados, pero el host no instala
  // reemplazos cosmeticos de recursos.
  DWORD custom_skin_hook;
  DWORD fullscreen_hook;
  DWORD help_url_hook;
  DWORD achievements_iat;
  DWORD invite_player_iat;

  // Bring-up del juego 0.13.2.
  DWORD game_malloc_iat;
  DWORD fn_resource_owner_ctor;
  DWORD resource_owner_slot;
  DWORD fn_game_d3d_init;
  DWORD fn_init_render_targets;
  DWORD fn_release_render_targets;  // game+0x5398F0, __thiscall(renderer)
  DWORD fn_device_resources_ctor;
  DWORD fn_app_main_ctor;
  DWORD app_platform_slot;
  DWORD fn_app_main_frame;

  // Entrada Win32 0.13.2. A diferencia de las ramas modernas, mouse/key se
  // construyen directamente con el allocator del juego y se despachan sobre
  // AppMain+4 (platform); texto conserva una fabrica propia con ABI de
  // registros reconstruida por el shim.
  DWORD fn_input_event_alloc;  // game+0x5798EA, __cdecl(size)
  DWORD fn_input_dispatch;     // game+0x3C10E0, __thiscall(platform,event)
  DWORD fn_translate_key;      // game+0x214CF0, ECX=0 / EDX=VK
  DWORD fn_make_text_event;    // game+0x219820, ECX=out / EDX=utf8 / stack seq

  // Layout de los objetos que revela FUN_629557e0.
  DWORD owner_renderer_offset;       // owner + 0x18 -> renderer
  DWORD owner_device_offset;         // owner + 0x10 -> ID3D11Device
  DWORD renderer_context_offset;       // renderer + 0x84 -> ID3D11DeviceContext
  DWORD renderer_swap_chain_offset;    // renderer + 0x74 -> IDXGISwapChain
  DWORD renderer_rtv_offset;           // renderer + 0x88 -> render target view
  DWORD renderer_width_offset;         // renderer + 0x60
  DWORD renderer_height_offset;        // renderer + 0x64
  DWORD renderer_depth_stencil_offset; // renderer + 0x90 -> depth-stencil view
  DWORD renderer_viewport_offset;      // renderer + 0x94 -> 24-byte viewport

  // MCPE_Host::AppMain es un holder pequeno en esta version.
  DWORD app_main_platform_offset;     // +0x04
  DWORD app_main_game_offset;         // +0x08
  DWORD app_main_frame_enabled_offset;// +0x10 (byte)

  // Campos de plataforma observados durante activacion, resize y cursor.
  // Ghidra no conserva aqui suficiente informacion de tipos para separar con
  // seguridad los dos objetos de plataforma que circulan por la funcion, asi
  // que los nombres describen solo el acceso observado, no una clase C++
  // definitiva.
  DWORD platform_state_offset;          // platform + 0x13c -> state
  DWORD platform_width_offset;          // platform + 0x140
  DWORD platform_height_offset;         // platform + 0x144
  DWORD platform_listener_list_offset;  // platform + 0x100

  // Estado HID apuntado por platform+0x13c.
  DWORD state_relative_offset;        // state + 0x45 (byte)
  DWORD state_cursor_command_offset;  // state + 0x48 (DWORD)
  DWORD state_cursor_toggle_offset;   // state + 0x6c (byte)
};

// Layout separado y verificado de 0.13.2. Es valido independientemente de la
// version actualmente detectada y sirve tambien a herramientas de verificacion.
const Version0132Layout& Layout0132() noexcept;

// Inicializa el modulo: guarda la base, detecta version y decide si estamos en
// Windows 7. Debe llamarse antes que cualquier otro accesor de este espacio.
//
// Se llama con el modulo del EXE (GetModuleHandleW(nullptr)), no con el del
// shim: los offsets son del juego.
Version Initialize(HMODULE game_module) noexcept;

// Base del EXE del juego (dword_6299602C en el original).
HMODULE Base() noexcept;

// Version detectada por Initialize.
Version Current() noexcept;

// True si el sistema es Windows 7 exactamente (dword_62996038).
//
// El original lo decide con GetVersion() y comparando la palabra baja con 262
// (0x0106 = major 6, minor 1). Es deliberadamente estricto: en 8 y 10 las rutas
// de compatibilidad DXGI sobran y hacen mas mal que bien.
bool IsWindows7() noexcept;

// Legacy aggregate accessor retained for tools and transition checks. Modern
// production subsystems use the sections injected by bootstrap instead.
const Layout& CurrentLayout() noexcept;

// Convierte un RVA en un puntero dentro del EXE del juego.
//
// Devuelve nullptr si el RVA es 0 (parche no aplicable a esta version) o si
// todavia no se ha llamado a Initialize.
void* Resolve(DWORD rva) noexcept;

}  // namespace shim::game
