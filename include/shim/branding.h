// Identidad del proyecto.
//
// TODO EL RESTO DEL CODIGO USA NOMBRES NEUTROS. Este es el unico fichero que
// menciona la marca, para que renombrar el shim (a opticraft, o a lo que sea)
// sea cambiar estas constantes y nada mas.
//
// Default values identify the ReUWP shim.

#pragma once

// Nombre del modulo. Debe cuadrar con OUTPUT_NAME en CMakeLists.txt y con
// LIBRARY en exports.def.
#define SHIM_MODULE_NAME "reuwp"

// Variable de entorno que activa la traza de diagnostico.
//
// Es comportamiento observable: quien depure el shim la define para ver la
// salida. Cambiarla al renombrar rompe cualquier guion o instruccion existente,
// asi que conviene decidirlo a conciencia.
#define SHIM_DEBUG_ENV_VAR "REUWP_DEBUG"

// Nombre visible para el usuario. Aparece en el titulo de la ventana y en las
// lineas de traza del arranque.
//
// A diferencia de SHIM_MODULE_NAME esto es texto de interfaz, no un nombre de
// fichero: puede llevar mayusculas y espacios.
#define SHIM_DISPLAY_NAME_W L"ReUWP"
#define SHIM_DISPLAY_NAME_A "ReUWP"
#define SHIM_DISPLAY_NAME_LENGTH 5

// Nombre de clase que devuelve IInspectable::GetRuntimeClassName en todos los
// objetos WinRT falsos.
//
// El juego nunca lo consulta, asi que su valor es libre. Se conserva el del
// original porque aparece en trazas de depuracion y facilita comparar.
#define SHIM_RUNTIME_CLASS_NAME L"ReUWP.Win32.RuntimeObject"
#define SHIM_RUNTIME_CLASS_NAME_LENGTH 25
