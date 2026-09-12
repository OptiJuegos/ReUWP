#include "shim/app_window.h"

#include "shim/branding.h"
#include "shim/config.h"
#include "shim/game_layout.h"
#include "shim/game_profile.h"
#include "shim/input.h"
#include "shim/log.h"
#include "shim/swapchain.h"

namespace shim::app_window {
namespace {

struct WindowState {
  HWND window = nullptr;  // dword_62996030

  // Deferred client size while the user drags a window border.
  // Zero means there is no pending resize.
  bool sizing = false;  // byte_62999490
  int deferred_width = 0;
  int deferred_height = 0;

  int client_width = kDefaultWidth;
  int client_height = kDefaultHeight;
  ResizeHandler resize_handler = nullptr;

  // State preserved by 0x6295C620 while entering borderless fullscreen.
  bool borderless_fullscreen = false;
  WINDOWPLACEMENT windowed_placement = {};
  LONG windowed_style = 0;
  LONG windowed_exstyle = 0;
};

WindowState g_state = {};

// Nombre de clase de ventana, por version del juego.
//
// Son distintos a proposito: dos versiones del juego pueden estar instaladas a
// la vez, y una clase de ventana compartida haria que la segunda encontrara
// registrada la de la primera con otro wndproc.
//
// No se deriva de la marca. Identifica al juego, no al shim, y es visible desde
// fuera con FindWindow: renombrar el shim no deberia cambiarlo.
const wchar_t* ClassName() noexcept {
  const game::VersionProfile* const profile = game::CurrentProfile();
  return profile != nullptr ? profile->window_class_name : L"ReUWPWin32";
}

const wchar_t* WindowTitle() noexcept {
  const game::VersionProfile* const profile = game::CurrentProfile();
  return profile != nullptr ? profile->window_title : SHIM_DISPLAY_NAME_W;
}

const wchar_t* ArrowCursor() noexcept {
  return reinterpret_cast<const wchar_t*>(0x7F00);
}

void SyncCursorAfterGeometryChange(HWND window) noexcept {
  input::InvalidateCursorClip();
  input::SyncCursorMode(window);
}

bool ShouldDeferInteractiveResize() noexcept {
  return REUWP_DEFER_INTERACTIVE_RESIZE || game::IsWindows7();
}

void BeginWindowInteraction(HWND window) noexcept {
  g_state.sizing = true;
  g_state.deferred_width = 0;
  g_state.deferred_height = 0;
  input::SetWindowInteractionActive(true);
  input::SyncCursorMode(window);
  log::Write("interactive window move/resize entered");
}

void ApplyDeferredResize(HWND window) noexcept {
  int width = g_state.deferred_width;
  int height = g_state.deferred_height;
  if (width == 0 || height == 0) {
    RECT client = {};
    if (::GetClientRect(window, &client)) {
      width = client.right - client.left;
      height = client.bottom - client.top;
    }
  }

  g_state.deferred_width = 0;
  g_state.deferred_height = 0;
  if (width > 0 && height > 0 &&
      (width != g_state.client_width || height != g_state.client_height)) {
    OnResize(width, height);
  }
}

void EndWindowInteraction(HWND window) noexcept {
  g_state.sizing = false;
  if (ShouldDeferInteractiveResize()) {
    ApplyDeferredResize(window);
  }
  input::SetWindowInteractionActive(false);
  SyncCursorAfterGeometryChange(window);
  log::Write("interactive window move/resize exited");
}

bool IsAltF4(UINT message, WPARAM wparam, LPARAM lparam) noexcept {
#if REUWP_ALT_F4_AS_ESCAPE
  return message == WM_SYSKEYDOWN && wparam == VK_F4 &&
         (lparam & 0x20000000) != 0;
#else
  (void)message;
  (void)wparam;
  (void)lparam;
  return false;
#endif
}

void PostEscapeTap() noexcept {
  input::PostKeyEdge(VK_ESCAPE, true);
  input::PostKeyEdge(VK_ESCAPE, false);
}

}  // namespace

HWND Handle() noexcept {
  return g_state.window;
}

bool RequestClose() noexcept {
  const HWND window = Handle();
  if (window != nullptr) {
    return ::PostMessageW(window, WM_CLOSE, 0, 0) != FALSE;
  }

  ::PostQuitMessage(0);
  return true;
}

void SetResizeHandler(ResizeHandler handler) noexcept {
  g_state.resize_handler = handler;
}

void ClientSize(int* width, int* height) noexcept {
  if (width != nullptr) {
    *width = g_state.client_width;
  }
  if (height != nullptr) {
    *height = g_state.client_height;
  }
}

void OnResize(int width, int height) noexcept {
  if (width <= 0 || height <= 0) {
    return;
  }
  g_state.client_width = width;
  g_state.client_height = height;

  char message[96];
  ::wsprintfA(message, "window resized to %dx%d", width, height);
  log::Write(message);

  // 0.13.2 tiene una ruta de resize mas larga: ademas de DXGI libera/recrea
  // los targets del juego y notifica MinecraftClient. Las ramas modernas usan
  // la ruta DXGI generica.
  if (g_state.resize_handler != nullptr) {
    if (!g_state.resize_handler(width, height)) {
      log::Write("version-specific window resize failed");
    }
  } else {
    d3d::Resize(width, height);
  }
}

bool SetBorderlessFullscreen(bool enabled) noexcept {
  if (g_state.window == nullptr) {
    return false;
  }

  bool changed = false;
  if (enabled && !g_state.borderless_fullscreen) {
    g_state.windowed_placement = {};
    g_state.windowed_placement.length = sizeof(g_state.windowed_placement);
    if (!::GetWindowPlacement(g_state.window, &g_state.windowed_placement)) {
      return false;
    }

    g_state.windowed_style = ::GetWindowLongW(g_state.window, GWL_STYLE);
    g_state.windowed_exstyle = ::GetWindowLongW(g_state.window, GWL_EXSTYLE);

    const HMONITOR monitor =
        ::MonitorFromWindow(g_state.window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (monitor == nullptr || !::GetMonitorInfoW(monitor, &info)) {
      return false;
    }

    // 6295C778..6295C791: conserva exactamente el filtrado de estilo del DLL.
    const LONG borderless_style =
        static_cast<LONG>((static_cast<DWORD>(g_state.windowed_style) & 0x7F30FFFFu) |
                          static_cast<DWORD>(WS_POPUP));
    ::SetWindowLongW(g_state.window, GWL_STYLE, borderless_style);
    ::SetWindowPos(g_state.window, nullptr, info.rcMonitor.left, info.rcMonitor.top,
                   info.rcMonitor.right - info.rcMonitor.left,
                   info.rcMonitor.bottom - info.rcMonitor.top, 0x270);
    g_state.borderless_fullscreen = true;
    changed = true;
    log::Write("entered Win32 borderless fullscreen");
  } else if (!enabled && g_state.borderless_fullscreen) {
    ::SetWindowLongW(g_state.window, GWL_STYLE, g_state.windowed_style);
    ::SetWindowLongW(g_state.window, GWL_EXSTYLE, g_state.windowed_exstyle);
    ::SetWindowPlacement(g_state.window, &g_state.windowed_placement);
    ::SetWindowPos(g_state.window, nullptr, 0, 0, 0, 0, 0x267);
    g_state.borderless_fullscreen = false;
    changed = true;
    log::Write("left Win32 borderless fullscreen");
  }

  // El DLL pone su byte de clip a cero solo cuando cambia de modo y luego llama
  // al sincronizador en todos los casos.
  if (changed) {
    input::InvalidateCursorClip();
  }
  input::SyncCursorMode(g_state.window);
  return true;
}

HWND Create(HINSTANCE module) noexcept {
  WNDCLASSEXW window_class = {};
  window_class.cbSize = sizeof(window_class);
  // CS_HREDRAW | CS_VREDRAW | CS_OWNDC: el juego dibuja el area cliente entera
  // en cada fotograma, asi que quiere repintado completo al cambiar de tamano y
  // un contexto de dispositivo propio.
  window_class.style = CS_VREDRAW | CS_HREDRAW | CS_OWNDC;
  window_class.lpfnWndProc = reinterpret_cast<WNDPROC>(&WindowProc);
  window_class.hInstance = module;
  window_class.hCursor = ::LoadCursorW(nullptr, ArrowCursor());
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.lpszClassName = ClassName();

  // ERROR_CLASS_ALREADY_EXISTS no es un fallo: el arranque puede ejecutarse dos
  // veces si el juego recarga el shim, y la clase sigue siendo valida.
  if (::RegisterClassExW(&window_class) == 0 &&
      ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    log::Write("RegisterClassExW failed");
    return nullptr;
  }

  const HWND window = ::CreateWindowExW(
      0, window_class.lpszClassName, WindowTitle(), kWindowStyle,
      CW_USEDEFAULT, CW_USEDEFAULT, kDefaultWidth, kDefaultHeight, nullptr,
      nullptr, module, nullptr);
  if (window == nullptr) {
    log::Write("CreateWindowExW failed");
    return nullptr;
  }

  g_state.window = window;
  input::RegisterRawMouse(window);
  // Hasta el primer clic el raton se queda absoluto y visible: el menu
  // principal se navega con el puntero, no con la vista.
  input::ArmCursorGuard();
  log::Write("HWND created");
  return window;
}

LRESULT SHIM_COM WindowProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) noexcept {
  switch (message) {
    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;

    case WM_CLOSE:
      ::DestroyWindow(window);
      return 0;

    case WM_PAINT: {
      // El juego pinta por Direct3D, no por GDI. Aqui solo hay que validar la
      // region: sin BeginPaint/EndPaint Windows reenvia WM_PAINT sin parar y el
      // bucle de mensajes no llega nunca al fotograma.
      PAINTSTRUCT paint = {};
      ::BeginPaint(window, &paint);
      ::EndPaint(window, &paint);
      return 0;
    }

    case WM_SIZE: {
      if (wparam == SIZE_MINIMIZED) {
        input::InvalidateCursorClip();
        return 0;
      }
      const int width = LOWORD(lparam);
      const int height = HIWORD(lparam);
      if (g_state.sizing && ShouldDeferInteractiveResize()) {
        g_state.deferred_width = width;
        g_state.deferred_height = height;
      } else {
        OnResize(width, height);
      }
      SyncCursorAfterGeometryChange(window);
      return 0;
    }

    case WM_ENTERSIZEMOVE:
      BeginWindowInteraction(window);
      return ::DefWindowProcW(window, message, wparam, lparam);

    case WM_EXITSIZEMOVE:
      EndWindowInteraction(window);
      return ::DefWindowProcW(window, message, wparam, lparam);

    case WM_MOVE:
    case WM_WINDOWPOSCHANGED:
      // Mover la ventana invalida el recorte del cursor en modo relativo.
      SyncCursorAfterGeometryChange(window);
      return ::DefWindowProcW(window, message, wparam, lparam);

    case WM_SETFOCUS:
      input::SyncCursorMode(window);
      return 0;

    case WM_KILLFOCUS:
      // Sin esto, salir de la ventana con una tecla de movimiento pulsada deja
      // al personaje andando: el WM_KEYUP se lo lleva la ventana que recibe el
      // foco y el juego nunca se entera.
      input::ReleaseAllKeys();
      input::SyncCursorMode(window);
      return 0;

    case WM_ACTIVATEAPP:
      if (wparam == FALSE) {
        input::ReleaseAllKeys();
      }
      input::SyncCursorMode(window);
      return 0;

    case WM_SETCURSOR:
      // En modo relativo el cursor se oculta a mano; hay que responder que el
      // mensaje esta tratado o Windows lo vuelve a dibujar en cada movimiento.
      if (LOWORD(lparam) == HTCLIENT && input::CursorIsRelative()) {
        ::SetCursor(nullptr);
        return TRUE;
      }
      return ::DefWindowProcW(window, message, wparam, lparam);

    // --- Raton -------------------------------------------------------------

    case WM_INPUT:
      input::PostRawMouse(reinterpret_cast<HRAWINPUT>(lparam));
      return ::DefWindowProcW(window, message, wparam, lparam);

    case WM_MOUSEMOVE:
      input::PostMouseMove(lparam);
      return 0;

    case WM_LBUTTONDOWN:
      // El primer clic es lo que libera la guarda del cursor inicial: a partir
      // de ahi el juego manda sobre el modo.
      if (input::CursorGuardActive()) {
        input::ReleaseCursorGuard();
        log::Write("initial main-menu cursor guard released by click");
      }
      ::SetFocus(window);
      ::SetCapture(window);
      input::PostMouseMove(lparam);
      input::PostPointer(static_cast<short>(LOWORD(lparam)),
                         static_cast<short>(HIWORD(lparam)), false,
                         input::PointerAction::kLeftButton, 1);
      return 0;

    case WM_RBUTTONDOWN:
      ::SetFocus(window);
      ::SetCapture(window);
      input::PostMouseMove(lparam);
      input::PostPointer(static_cast<short>(LOWORD(lparam)),
                         static_cast<short>(HIWORD(lparam)), false,
                         input::PointerAction::kRightButton, 1);
      return 0;

    case WM_MBUTTONDOWN:
      ::SetFocus(window);
      ::SetCapture(window);
      input::PostMouseMove(lparam);
      input::PostPointer(static_cast<short>(LOWORD(lparam)),
                         static_cast<short>(HIWORD(lparam)), false,
                         input::PointerAction::kMiddleButton, 1);
      return 0;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
      const auto action = message == WM_LBUTTONUP
                              ? input::PointerAction::kLeftButton
                              : message == WM_RBUTTONUP
                                    ? input::PointerAction::kRightButton
                                    : input::PointerAction::kMiddleButton;
      input::PostMouseMove(lparam);
      input::PostPointer(static_cast<short>(LOWORD(lparam)),
                         static_cast<short>(HIWORD(lparam)), false, action, 0);
      // En modo relativo la captura no se suelta: el raton tiene que seguir
      // dentro de la ventana aunque no haya ningun boton pulsado.
      if (!input::CursorIsRelative() && ::GetCapture() == window) {
        ::ReleaseCapture();
      }
      return 0;
    }

    case WM_MOUSEWHEEL: {
      // La rueda llega como multiplo de 120 con signo; el juego espera un solo
      // byte con signo, asi que se recorta a [-128, 127].
      int delta = GET_WHEEL_DELTA_WPARAM(wparam);
      if (delta < -127) {
        delta = -128;
      } else if (delta > 127) {
        delta = 127;
      }
      input::PostPointer(0, 0, false, input::PointerAction::kWheel,
                         static_cast<unsigned char>(delta));
      return 0;
    }

    // --- Teclado -----------------------------------------------------------

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      const bool is_repeat = (lparam & 0x40000000) != 0;
      if (IsAltF4(message, wparam, lparam)) {
        if (!is_repeat) {
          PostEscapeTap();
        }
        return 0;
      }

      // El Enter de 0.15.10 con el chat abierto envia el mensaje. Va por un
      // gancho propio porque el ChatScreen nativo no reacciona a la tecla.
      if (wparam == VK_RETURN && input::TextInputActive()) {
        if (!is_repeat && input::RequestSubmit()) {
          input::SetSwallowNextChar();
        }
        return 0;
      }

      // La T abre el chat. Su WM_CHAR llegaria despues y escribiria una 't' en
      // el cuadro recien abierto, asi que se descarta.
      if (wparam == 'T' && !input::TextInputActive() && !is_repeat) {
        input::SetSwallowNextChar();
      }

      input::PostKeyEdge(static_cast<unsigned int>(wparam), true);
      return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP:
#if REUWP_ALT_F4_AS_ESCAPE
      if (message == WM_SYSKEYUP && wparam == VK_F4) {
        return 0;
      }
#endif
      input::PostKeyEdge(static_cast<unsigned int>(wparam), false);
      return 0;

    case WM_CHAR:
      input::FeedUtf16Unit(static_cast<wchar_t>(wparam));
      return 0;

    case WM_UNICHAR:
      // Protocolo de UNICHAR: responder TRUE a la consulta anuncia que la
      // ventana acepta caracteres fuera del plano basico.
      if (wparam == UNICODE_NOCHAR) {
        return TRUE;
      }
      if (input::TextInputActive()) {
        input::PostChar(static_cast<unsigned int>(wparam));
      }
      return 0;

    default:
      return ::DefWindowProcW(window, message, wparam, lparam);
  }
}

}  // namespace shim::app_window
