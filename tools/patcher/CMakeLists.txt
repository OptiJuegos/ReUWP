cmake_minimum_required(VERSION 3.20)

project(reuwp_patch LANGUAGES CXX)

# Independiente del shim a proposito: este es un programa de escritorio que se
# ejecuta una vez sobre un fichero, no codigo que se carga dentro del juego. El
# shim se compila obligatoriamente en x86 de 32 bits; este no tiene por que,
# aunque build.bat lo configure asi para no salirse del resto del proyecto.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(reuwp_patch
  src/main.cpp
  src/import_compat.cpp
  src/patcher.cpp
  src/pe.cpp
  src/section.cpp
)

# El unico punto de contacto con el shim es shim/image_info.h, que describe el
# bloque que este programa escribe y el shim lee. Compartir la cabecera en vez
# de copiar la estructura es lo que impide que las dos mitades se separen.
target_include_directories(reuwp_patch PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_definitions(reuwp_patch PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
  target_compile_options(reuwp_patch PRIVATE /W4 /permissive- /EHsc)
endif()
