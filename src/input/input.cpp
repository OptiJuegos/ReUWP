#include "shim/input.h"

#include "shim/app_window.h"
#include "shim/game_profile.h"
#include "shim/input_backend.h"
#include "shim/log.h"

namespace shim::input {
namespace {

struct InputState {
  bool enabled = false;
  bool reported_unavailable = false;

  bool requested_relative = false;
  bool cursor_relative = false;
  bool clip_is_current = false;
  bool cursor_guard = false;
  bool window_interaction_active = false;
  bool raw_mouse_available = false;

  bool text_input_active = false;
  wchar_t pending_high_surrogate = 0;
  bool swallow_next_char = false;

  bool key_down[256] = {};

  bool logged_requested = false;
  bool logged_actual = false;
  int logged_command = 0;
  bool logged_anything = false;

  unsigned int mouse_traces = 0;
  unsigned int key_traces = 0;
  unsigned int text_traces = 0;
};

InputState g_state = {};
constexpr unsigned int kMaxMouseTraces = 12;
constexpr unsigned int kMaxKeyTraces = 24;
constexpr unsigned int kMaxTextTraces = 32;

const wchar_t* ArrowCursor() noexcept {
  return reinterpret_cast<const wchar_t*>(0x7F00);
}

const game::InputOperations* CurrentOperations() noexcept {
  const game::VersionProfile* profile = game::CurrentProfile();
  return profile != nullptr ? profile->input : nullptr;
}

void ClipToClient(HWND window) noexcept {
  RECT client = {};
  if (!::GetClientRect(window, &client)) {
    return;
  }
  POINT top_left = {client.left, client.top};
  POINT bottom_right = {client.right, client.bottom};
  if (!::ClientToScreen(window, &top_left) ||
      !::ClientToScreen(window, &bottom_right)) {
    return;
  }
  const RECT screen = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  ::ClipCursor(&screen);
  g_state.clip_is_current = true;
}

void ReleaseCursor(HWND window) noexcept {
  ::ClipCursor(nullptr);
  if (::GetCapture() == window) {
    ::ReleaseCapture();
  }
  while (::ShowCursor(TRUE) < 0) {
  }
  ::SetCursor(::LoadCursorW(nullptr, ArrowCursor()));
}

}  // namespace

namespace backend {

int EncodeUtf8(unsigned int codepoint, char (&utf8)[5]) noexcept {
  if (codepoint <= 0x7F) {
    utf8[0] = static_cast<char>(codepoint);
    utf8[1] = '\0';
    return 1;
  }
  if (codepoint <= 0x7FF) {
    utf8[0] = static_cast<char>((codepoint >> 6) | 0xC0);
    utf8[1] = static_cast<char>((codepoint & 0x3F) | 0x80);
    utf8[2] = '\0';
    return 2;
  }
  if (codepoint <= 0xFFFF) {
    utf8[0] = static_cast<char>((codepoint >> 12) | 0xE0);
    utf8[1] = static_cast<char>(((codepoint >> 6) & 0x3F) | 0x80);
    utf8[2] = static_cast<char>((codepoint & 0x3F) | 0x80);
    utf8[3] = '\0';
    return 3;
  }
  utf8[0] = static_cast<char>((codepoint >> 18) | 0xF0);
  utf8[1] = static_cast<char>(((codepoint >> 12) & 0x3F) | 0x80);
  utf8[2] = static_cast<char>(((codepoint >> 6) & 0x3F) | 0x80);
  utf8[3] = static_cast<char>((codepoint & 0x3F) | 0x80);
  utf8[4] = '\0';
  return 4;
}

void ActivateTextInput() noexcept {
  g_state.text_input_active = true;
  g_state.pending_high_surrogate = 0;
}

void DeactivateTextInput() noexcept {
  g_state.text_input_active = false;
  g_state.pending_high_surrogate = 0;
  g_state.swallow_next_char = false;
}

bool* TextInputActiveFlag() noexcept {
  return &g_state.text_input_active;
}

}  // namespace backend

void SetAppMain(void* app_main) noexcept {
  const game::InputOperations* operations = CurrentOperations();
  if (operations != nullptr && operations->set_app_main != nullptr) {
    operations->set_app_main(app_main);
  }
}

bool InstallHooks() noexcept {
  const game::InputOperations* operations = CurrentOperations();
  return operations == nullptr || operations->install_hooks == nullptr ||
         operations->install_hooks();
}

bool InstallLifecycleHooks() noexcept {
  const game::InputOperations* operations = CurrentOperations();
  return operations == nullptr || operations->install_lifecycle_hooks == nullptr ||
         operations->install_lifecycle_hooks();
}

bool RequestSubmit() noexcept {
  const game::InputOperations* operations = CurrentOperations();
  return operations != nullptr && operations->request_submit != nullptr &&
         operations->request_submit();
}

void PumpSubmit() noexcept {
  const game::InputOperations* operations = CurrentOperations();
  if (operations != nullptr && operations->pump_submit != nullptr) {
    operations->pump_submit();
  }
}

void SetEnabled(bool enabled) noexcept {
  g_state.enabled = enabled;
  if (enabled) {
    g_state.reported_unavailable = false;
  }
}

void* Handler() noexcept {
  const game::InputOperations* operations = CurrentOperations();
  return operations != nullptr && operations->handler != nullptr
             ? operations->handler()
             : nullptr;
}

bool RegisterRawMouse(HWND window) noexcept {
  if (window == nullptr) {
    return false;
  }

  RAWINPUTDEVICE device = {};
  device.usUsagePage = 0x01;
  device.usUsage = 0x02;
  device.dwFlags = 0;
  device.hwndTarget = window;
  g_state.raw_mouse_available =
      ::RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
  log::Write(g_state.raw_mouse_available ? "Raw Input mouse registered"
                                   : "Raw Input mouse unavailable; using WM_MOUSEMOVE fallback");
  return g_state.raw_mouse_available;
}

void PostRawMouse(HRAWINPUT input_handle) noexcept {
  if (!g_state.cursor_relative || !g_state.raw_mouse_available || input_handle == nullptr) {
    return;
  }

  RAWINPUT input = {};
  UINT size = sizeof(input);
  if (::GetRawInputData(input_handle, RID_INPUT, &input, &size,
                        sizeof(RAWINPUTHEADER)) != sizeof(input) ||
      input.header.dwType != RIM_TYPEMOUSE) {
    return;
  }

  if ((input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
    g_state.raw_mouse_available = false;
    log::Write("absolute Raw Input mouse detected; using WM_MOUSEMOVE fallback");
    return;
  }

  const LONG delta_x = input.data.mouse.lLastX;
  const LONG delta_y = input.data.mouse.lLastY;
  if (delta_x == 0 && delta_y == 0) {
    return;
  }
  PostPointer(static_cast<int>(delta_x), static_cast<int>(delta_y), true,
              PointerAction::kMove, 0);
}

void RequestCursorMode(bool relative) noexcept {
  g_state.requested_relative = relative;
}

bool CursorIsRelative() noexcept {
  return g_state.cursor_relative;
}

bool CursorGuardActive() noexcept {
  return g_state.cursor_guard;
}

void ArmCursorGuard() noexcept {
  g_state.cursor_guard = true;
}

void InvalidateCursorClip() noexcept {
  g_state.clip_is_current = false;
}

void SetWindowInteractionActive(bool active) noexcept {
  if (g_state.window_interaction_active == active) {
    return;
  }
  g_state.window_interaction_active = active;
  g_state.clip_is_current = false;
}

void ReleaseCursorGuard() noexcept {
  g_state.cursor_guard = false;
  g_state.requested_relative = false;
  g_state.clip_is_current = false;
}

void SyncCursorMode(HWND window) noexcept {
  const game::InputOperations* operations = CurrentOperations();
  bool game_relative = false;
  int game_command = 0;
  if (operations != nullptr && operations->read_cursor_state != nullptr) {
    operations->read_cursor_state(&game_relative, &game_command);
  }

  if (!g_state.logged_anything || g_state.requested_relative != g_state.logged_requested ||
      game_relative != g_state.logged_actual || game_command != g_state.logged_command) {
    char message[144];
    ::wsprintfA(message, "HID mouse mode: requested=%d actual=%d command=%d",
                g_state.requested_relative ? 1 : 0, game_relative ? 1 : 0,
                game_command);
    log::Write(message);
    g_state.logged_requested = g_state.requested_relative;
    g_state.logged_actual = game_relative;
    g_state.logged_command = game_command;
    g_state.logged_anything = true;
  }

  const bool want_relative = g_state.requested_relative &&
                             ::GetForegroundWindow() == window &&
                             !::IsIconic(window) &&
                             !g_state.window_interaction_active;

  if (!want_relative) {
    if (!g_state.cursor_relative) {
      return;
    }
    g_state.cursor_relative = false;
    if (operations != nullptr && operations->write_cursor_mode != nullptr) {
      operations->write_cursor_mode(false);
    }
    ReleaseCursor(window);
    log::Write("relative mouse released");
    return;
  }

  if (!g_state.cursor_relative) {
    g_state.cursor_relative = true;
    ::SetCapture(window);
    while (::ShowCursor(FALSE) >= 0) {
    }
    if (operations != nullptr && operations->write_cursor_mode != nullptr) {
      operations->write_cursor_mode(true);
    }
    g_state.clip_is_current = false;
    ClipToClient(window);
    log::Write("relative mouse captured");
    return;
  }

  if (!g_state.clip_is_current || ::GetCapture() != window) {
    ::SetCapture(window);
    ClipToClient(window);
    log::Write("relative mouse clip refreshed");
  }
}

void PostPointer(int x, int y, bool relative, PointerAction action,
                 unsigned char pressed) noexcept {
  if (!g_state.enabled) {
    if (!g_state.reported_unavailable) {
      g_state.reported_unavailable = true;
      log::Write("Win32 input injection is not available");
    }
    return;
  }

  const game::InputOperations* operations = CurrentOperations();
  if (operations == nullptr || operations->post_pointer == nullptr ||
      !operations->post_pointer(x, y, relative,
                                static_cast<unsigned char>(action), pressed)) {
    return;
  }

  if (g_state.mouse_traces < kMaxMouseTraces) {
    char message[144];
    ::wsprintfA(message, "mouse event: x=%d y=%d action=%u pressed=%u", x, y,
                static_cast<unsigned int>(action),
                static_cast<unsigned int>(pressed));
    log::Write(message);
    ++g_state.mouse_traces;
  }
}

void PostMouseMove(LPARAM position) noexcept {
  const int x = static_cast<short>(LOWORD(position));
  const int y = static_cast<short>(HIWORD(position));

  if (!g_state.cursor_relative) {
    PostPointer(x, y, false, PointerAction::kMove, 0);
    return;
  }
  if (g_state.raw_mouse_available) {
    return;
  }

  const HWND window = app_window::Handle();
  RECT client = {};
  if (!::GetClientRect(window, &client)) {
    return;
  }

  const int center_x = (client.right - client.left) / 2;
  const int center_y = (client.bottom - client.top) / 2;
  const int delta_x = x - center_x;
  const int delta_y = y - center_y;
  if (delta_x == 0 && delta_y == 0) {
    return;
  }

  PostPointer(delta_x, delta_y, true, PointerAction::kMove, 0);

  POINT center = {center_x, center_y};
  if (::ClientToScreen(window, &center)) {
    ::SetCursorPos(center.x, center.y);
  }
}

void PostKey(unsigned int virtual_key, bool down) noexcept {
  if (!g_state.enabled || virtual_key == 0 || virtual_key > 255) {
    return;
  }

  const game::InputOperations* operations = CurrentOperations();
  if (operations == nullptr || operations->post_key == nullptr ||
      !operations->post_key(virtual_key, down)) {
    return;
  }

  if (g_state.key_traces < kMaxKeyTraces) {
    char message[144];
    ::wsprintfA(message, "keyboard event: key=%u state=%u", virtual_key,
                down ? 1u : 0u);
    log::Write(message);
    ++g_state.key_traces;
  }
}

void PostKeyEdge(unsigned int virtual_key, bool down) noexcept {
  if (virtual_key == 0 || virtual_key > 255 || !g_state.enabled) {
    return;
  }
  if (down && g_state.key_down[virtual_key]) {
    return;
  }
  g_state.key_down[virtual_key] = down;
  PostKey(virtual_key, down);
}

void PostChar(unsigned int codepoint) noexcept {
  if (codepoint > 0x10FFFF) {
    return;
  }
  if (codepoint < 0x20 && codepoint != 13 && codepoint != 8) {
    return;
  }
  if ((codepoint & 0x1FF800) == 0xD800 || !g_state.enabled) {
    return;
  }

  const game::InputOperations* operations = CurrentOperations();
  if (operations == nullptr || operations->post_char == nullptr ||
      !operations->post_char(codepoint)) {
    return;
  }

  if (g_state.text_traces < kMaxTextTraces) {
    char utf8[5] = {};
    const int length = backend::EncodeUtf8(codepoint, utf8);
    char message[144];
    ::wsprintfA(message, "text event queued: codepoint=U+%04lX bytes=%u",
                codepoint, static_cast<unsigned int>(length));
    log::Write(message);
    ++g_state.text_traces;
  }
}

void FeedUtf16Unit(wchar_t unit) noexcept {
  if (!g_state.text_input_active) {
    g_state.pending_high_surrogate = 0;
    return;
  }

  if (g_state.swallow_next_char) {
    g_state.swallow_next_char = false;
    return;
  }

  const unsigned int value = static_cast<unsigned int>(unit);
  if ((value & 0xFC00) == 0xD800) {
    g_state.pending_high_surrogate = unit;
    return;
  }

  unsigned int codepoint = value;
  if ((value & 0xFC00) == 0xDC00 && g_state.pending_high_surrogate != 0) {
    codepoint = ((static_cast<unsigned int>(g_state.pending_high_surrogate) - 0xD800)
                 << 10) +
                (value - 0xDC00) + 0x10000;
  }
  g_state.pending_high_surrogate = 0;
  PostChar(codepoint);
}

void ReleaseAllKeys() noexcept {
  for (unsigned int key = 1; key < 256; ++key) {
    if (g_state.key_down[key]) {
      g_state.key_down[key] = false;
      PostKey(key, false);
    }
  }
  g_state.pending_high_surrogate = 0;
  g_state.swallow_next_char = false;
}

void SetSwallowNextChar() noexcept {
  g_state.swallow_next_char = true;
}

bool TextInputActive() noexcept {
  return g_state.text_input_active;
}

}  // namespace shim::input
