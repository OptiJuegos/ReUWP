#include "shim/network.h"

#include <winsock2.h>

#include "shim/log.h"

namespace shim::net {
namespace {

bool g_resolved = false;                              // byte_62998EDC
wchar_t g_address_text[kAddressTextCapacity] = {};

// Escribe "a.b.c.d" en el buffer de texto (sub_6294B380).
void FormatAddress(const uint8_t address[4]) noexcept {
  ::wsprintfW(g_address_text, L"%u.%u.%u.%u", address[0], address[1],
              address[2], address[3]);
}

bool IsLoopback(const uint8_t address[4]) noexcept {
  return address[0] == 127;
}

bool IsUnspecified(const uint8_t address[4]) noexcept {
  return address[0] == 0 && address[1] == 0 && address[2] == 0 &&
         address[3] == 0;
}

// Recorre la lista de direcciones del host quedandose con la primera utilizable.
//
// Devuelve nullptr si la lista esta vacia. Si todas son loopback o 0.0.0.0
// devuelve la primera: es mejor anunciar algo coherente que nada.
const uint8_t* PickBestAddress(hostent* host) noexcept {
  if (host == nullptr || host->h_addrtype != AF_INET || host->h_length != 4 ||
      host->h_addr_list == nullptr) {
    return nullptr;
  }

  const uint8_t* first_seen = nullptr;
  for (char** entry = host->h_addr_list; *entry != nullptr; ++entry) {
    const auto* address = reinterpret_cast<const uint8_t*>(*entry);
    if (first_seen == nullptr) {
      first_seen = address;
    }
    if (!IsLoopback(address) && !IsUnspecified(address)) {
      return address;
    }
  }
  return first_seen;
}

}  // namespace

void ResolveLocalAddress() noexcept {
  if (g_resolved) {
    return;
  }
  // El respaldo se deja puesto ANTES de intentar nada, para que cualquier
  // salida temprana deje el texto en un estado valido.
  FormatAddress(kFallbackAddress);
  g_resolved = true;

  // ws2_32 se carga en tiempo de ejecucion en vez de enlazarla: el shim debe
  // arrancar aunque falte o falle, y sin red el juego sigue siendo jugable en
  // solitario.
  const HMODULE winsock = ::LoadLibraryW(L"ws2_32.dll");
  if (winsock == nullptr) {
    return;
  }

  using WSAStartupFn = int(WINAPI*)(WORD, LPWSADATA);
  using GetHostNameFn = int(WINAPI*)(char*, int);
  using GetHostByNameFn = hostent*(WINAPI*)(const char*);

  const auto startup = reinterpret_cast<WSAStartupFn>(
      ::GetProcAddress(winsock, "WSAStartup"));
  const auto get_host_name = reinterpret_cast<GetHostNameFn>(
      ::GetProcAddress(winsock, "gethostname"));
  const auto get_host_by_name = reinterpret_cast<GetHostByNameFn>(
      ::GetProcAddress(winsock, "gethostbyname"));

  if (startup == nullptr || get_host_name == nullptr ||
      get_host_by_name == nullptr) {
    return;
  }

  WSADATA data;
  memset(&data, 0, sizeof(data));
  if (startup(MAKEWORD(2, 2), &data) != 0) {
    return;
  }

  char host_name[256];
  memset(host_name, 0, sizeof(host_name));
  if (get_host_name(host_name, sizeof(host_name) - 1) != 0) {
    return;
  }

  const uint8_t* address = PickBestAddress(get_host_by_name(host_name));
  if (address != nullptr) {
    FormatAddress(address);
    log::Write("Win32 NetworkInformation: local IPv4 HostName returned");
  }

  // No se llama a WSACleanup a proposito: el juego usa Winsock por su cuenta y
  // cerrar aqui le decrementaria el contador de inicializacion antes de tiempo.
}

const wchar_t* LocalAddressText() noexcept {
  ResolveLocalAddress();
  return g_address_text;
}

}  // namespace shim::net
