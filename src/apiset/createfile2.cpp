// CreateFile2 para Windows 7.
//
// Original: sub_62941030, exportada como CreateFile2 (ordinal 4).
//
// CreateFile2 se introdujo en Windows 8 y es la unica forma que tiene el CRT
// de UWP de abrir ficheros. En Win7 no existe, asi que se reimplementa
// desempaquetando CREATEFILE2_EXTENDED_PARAMETERS sobre CreateFileW.

// Este fichero apunta a Windows 7 a proposito, y ES la razon de que pueda
// definir CreateFile2 con su nombre real.
//
// Con el target por defecto (Win10) el SDK declara CreateFile2 en fileapi.h como
// __declspec(dllimport), y definir una funcion declarada dllimport es un error;
// habria que implementarla con otro nombre y exportarla por alias. Bajando
// _WIN32_WINNT a 0x0601 esa declaracion desaparece (esta guardada por
// _WIN32_WINNT >= _WIN32_WINNT_WIN8) y el nombre queda libre.
//
// Es ademas lo correcto conceptualmente: el codigo de este fichero existe
// justamente porque la plataforma objetivo es Windows 7. Se aisla aqui para no
// esconder la superficie WinRT (WindowsCreateString y demas) que el resto del
// shim si necesita declarada.
#undef _WIN32_WINNT
#undef WINVER
#undef NTDDI_VERSION
#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#define NTDDI_VERSION 0x06010000

#include "shim/common.h"
#include "shim/runtime_trace.h"

// CREATEFILE2_EXTENDED_PARAMETERS tambien esta guardada por Win8, asi que con el
// target en Win7 hay que declararla. Layout tomado de minwinbase.h; no puede
// cambiar, porque quien construye la estructura es el CRT del juego.
#if !defined(CREATEFILE2_EXTENDED_PARAMETERS_DEFINED)
#define CREATEFILE2_EXTENDED_PARAMETERS_DEFINED
typedef struct _CREATEFILE2_EXTENDED_PARAMETERS {
  DWORD dwSize;
  DWORD dwFileAttributes;
  DWORD dwFileFlags;
  DWORD dwSecurityQosFlags;
  LPSECURITY_ATTRIBUTES lpSecurityAttributes;
  HANDLE hTemplateFile;
} CREATEFILE2_EXTENDED_PARAMETERS, *PCREATEFILE2_EXTENDED_PARAMETERS,
    *LPCREATEFILE2_EXTENDED_PARAMETERS;
#endif

extern "C" {

HANDLE SHIM_COM CreateFile2(LPCWSTR file_name,
                            DWORD desired_access,
                            DWORD share_mode,
                            DWORD creation_disposition,
                            LPCREATEFILE2_EXTENDED_PARAMETERS extended) {
  shim::runtime_trace::Record(shim::runtime_trace::BoundaryKind::Storage,
                        "CreateFile2.begin",
                        reinterpret_cast<uintptr_t>(file_name), desired_access);
  LPSECURITY_ATTRIBUTES security_attributes = nullptr;
  DWORD flags_and_attributes = 0;
  HANDLE template_file = nullptr;

  if (extended != nullptr) {
    // CreateFileW recibe atributos y flags en un unico parametro, mientras que
    // CreateFile2 los separa en tres campos. Se combinan con OR porque ocupan
    // rangos de bits disjuntos.
    //
    // Nota: el original no valida extended->dwSize, asi que una estructura de
    // una version futura con campos extra se aceptaria igual. Se mantiene el
    // comportamiento: el juego siempre pasa la estructura de su propio SDK.
    flags_and_attributes = extended->dwSecurityQosFlags |
                           extended->dwFileAttributes |
                           extended->dwFileFlags;
    security_attributes = extended->lpSecurityAttributes;
    template_file = extended->hTemplateFile;
  }

  const HANDLE result = ::CreateFileW(file_name, desired_access, share_mode,
                                      security_attributes, creation_disposition,
                                      flags_and_attributes, template_file);
  const DWORD error = result == INVALID_HANDLE_VALUE ? ::GetLastError() : ERROR_SUCCESS;
  shim::runtime_trace::Record(shim::runtime_trace::BoundaryKind::Storage,
                        "CreateFile2.end",
                        reinterpret_cast<uintptr_t>(result), creation_disposition,
                        static_cast<long>(error));
  if (result == INVALID_HANDLE_VALUE) {
    ::SetLastError(error);
  }
  return result;
}

// Ganchos de delay-load (sub_62941000 / 62941010 / 62941020).
//
// Los tres devuelven 0 incondicionalmente. No resuelven nada: existen para
// desactivar el manejo de fallos de delay-load del CRT, que en el proceso del
// juego abortaria al no encontrar las api-set. Devolver 0 deja que el fallo se
// propague en silencio y que el shim resuelva el import por su cuenta.

int SHIM_COM ResolveDelayLoadedAPI(int, int, int, int, int, int) {
  return 0;
}

int SHIM_COM ResolveDelayLoadsFromDll(int, int, int) {
  return 0;
}

int SHIM_COM DelayLoadFailureHook(int, int) {
  return 0;
}

}  // extern "C"

namespace shim::apiset {

// El arranque necesita esta direccion para meterla en la IAT del juego, y solo
// se puede tomar desde aqui: en cualquier otra unidad de traduccion el nombre
// CreateFile2 lo ha reclamado ya la declaracion del SDK.
void* CreateFile2Address() noexcept {
  return reinterpret_cast<void*>(&CreateFile2);
}

}  // namespace shim::apiset
