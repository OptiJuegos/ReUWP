cmake_minimum_required(VERSION 3.20)

project(reuwp LANGUAGES CXX)

# El binario original es x86 de 32 bits y no hay margen: el juego objetivo es
# x86 y los offsets codificados a mano asumen punteros de 4 bytes.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR
    "ReUWP must be built for 32-bit x86. "
    "Configura con: cmake -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Nombre del modulo de salida. Al renombrar el shim hay que tocar tambien
# LIBRARY en src/exports.def y SHIM_MODULE_NAME en include/shim/branding.h.
set(SHIM_OUTPUT_NAME "reuwp")

add_library(reuwp SHARED
  src/apiset/createfile2.cpp
  src/apiset/winrt_string.cpp
  src/apiset/winrt_string_ops.cpp
  src/apiset/winrt_apartment.cpp
  src/apiset/winrt_error_object.cpp
  src/apiset/winrt_error.cpp
  src/apiset/winrt_activation.cpp
  src/apiset/win7_compat.cpp
  src/bootstrap/app_window.cpp
  src/bootstrap/bootstrap.cpp
  src/config/runtime_config.cpp
  src/main.cpp
  src/game/game_layout.cpp
  src/bootstrap/local_folder.cpp
  src/d3d/d3d11_forward.cpp
  src/d3d/swapchain.cpp
  src/d3d/swapchain_create.cpp
  src/d3d/swapchain_present.cpp
  src/d3d/swapchain_context.cpp
  src/d3d/swapchain_resize.cpp
  src/d3d/swapchain_win7.cpp
  src/d3d/swapchain_modern.cpp
  src/diag/crash_report.cpp
  src/diag/log.cpp
  src/diag/runtime_trace.cpp
  src/diag/watchdog.cpp
  src/fmod/fmod_bridge.cpp
  src/fmod/fmod_loader.cpp
  src/fmod/fmod_state.cpp
  src/fmod/fmod_runtime.cpp
  src/game/game_profile.cpp
  src/game/render_distance_policy.cpp
  src/versions/v0_13_2/profile.cpp
  src/versions/v0_15_10/profile.cpp
  src/versions/v1_1_5/profile.cpp
  src/fmod/fmod_install.cpp
  src/versions/v0_13_2/fmod.cpp
  src/versions/v0_15_10/fmod.cpp
  src/versions/v1_1_5/fmod.cpp
  src/hooks/compat_hooks.cpp
  src/hooks/d3d_compile_compat.cpp
  src/hooks/d3d_device_compat.cpp
  src/hooks/vccorlib_compat.cpp
  src/hooks/patch.cpp
  src/hooks/x86_abi.cpp
  src/hooks/x86_patch.cpp
  src/hooks/stdio_overrides.cpp
  src/input/input.cpp
  src/versions/modern/input_common.cpp
  src/versions/v0_13_2/input.cpp
  src/versions/v0_15_10/input.cpp
  src/versions/v1_1_5/input.cpp
  src/versions/v0_13_2/asm_hooks.cpp
  src/versions/v0_13_2/hooks_runtime.cpp
  src/versions/v0_13_2/hooks_ui.cpp
  src/versions/v0_13_2/runtime.cpp
  src/versions/modern/runtime_core.cpp
  src/versions/v0_15_10/runtime.cpp
  src/versions/v1_1_5/runtime.cpp
  src/versions/v0_15_10/asm_hooks.cpp
  src/versions/v0_15_10/patches.cpp
  src/versions/v0_15_10/patches_runtime.cpp
  src/versions/v0_15_10/patches_render.cpp
  src/versions/v0_15_10/hooks_ui.cpp
  src/versions/v0_15_10/hooks_video_options.cpp
  src/versions/v0_15_10/hooks_lifecycle.cpp
  src/versions/v0_15_10/hooks_file_browser.cpp
  src/versions/v0_15_10/hooks_scheduler.cpp
  src/versions/v1_1_5/asm_hooks.cpp
  src/versions/v1_1_5/patches.cpp
  src/versions/v1_1_5/patches_runtime.cpp
  src/versions/v1_1_5/patches_render.cpp
  src/versions/v1_1_5/hooks_ui.cpp
  src/versions/v1_1_5/hooks_video_options.cpp
  src/versions/v1_1_5/hooks_lifecycle.cpp
  src/network/network.cpp
  src/storage/custom_skin.cpp
  src/storage/file_picker.cpp
  src/storage/storage.cpp
  src/store/store.cpp
  src/util/memory.cpp
  src/util/text_patch.cpp
  src/winrt/activation.cpp
  src/winrt/app_model.cpp
  src/winrt/app_model_identity.cpp
  src/winrt/app_model_globalization.cpp
  src/winrt/app_model_launcher.cpp
  src/winrt/app_model_threading.cpp
  src/winrt/app_model_xaml.cpp
  src/winrt/core_window.cpp
  src/winrt/crypto_buffer.cpp
  src/winrt/display_mouse.cpp
  src/winrt/platform_info.cpp
  src/winrt/runtime_init.cpp
  src/winrt/system_services.cpp
  src/winrt/system_collections.cpp
  src/winrt/system_network.cpp
  src/winrt/system_resources.cpp
  src/winrt/system_speech.cpp
  src/winrt/winrt_object.cpp
  src/winrt/winrt_vtable.cpp
  src/exports.def
)

target_include_directories(reuwp PRIVATE include)

target_compile_definitions(reuwp PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  UNICODE
  _UNICODE
  # El original usa las funciones "inseguras" del CRT (fopen, lstrcpyW...) y esta
  # reconstruccion las conserva a proposito: son las que importa el binario y
  # cambiarlas alteraria el comportamiento observable ante rutas limite.
  _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
  target_compile_options(reuwp PRIVATE
    /W4
    /permissive-
    /GR-        # el original no usa RTTI
    /EHsc
  )
  # El shim se carga dentro del proceso del juego y parchea codigo en caliente.
  # Debe reubicarse limpiamente, pero no puede depender de que el CRT este
  # inicializado antes que Win32Bootstrap.
  target_link_options(reuwp PRIVATE /DYNAMICBASE /NXCOMPAT)
endif()

target_link_libraries(reuwp PRIVATE
  user32
  shell32
  comdlg32
  ole32
  oleaut32
  psapi
  ws2_32
)

set_target_properties(reuwp PROPERTIES
  OUTPUT_NAME "${SHIM_OUTPUT_NAME}"
  PREFIX ""
)
