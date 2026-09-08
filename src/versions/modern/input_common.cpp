#include "shim/modern_input.h"

#include "shim/game_layout.h"
#include "shim/input_backend.h"
#include "shim/log.h"
#include "shim/x86_abi.h"

namespace shim::versions::modern::input_backend {
namespace {

using shim::input::backend::Field;
using shim::input::backend::GameFunction;
using shim::input::backend::ReadPointer;

struct HandlerCache {
  void* handler = nullptr;
  void* event_sink = nullptr;
  unsigned char* cursor_mode = nullptr;
  unsigned char* cursor_command = nullptr;
  bool cursor_fields_resolved = false;
};

struct InputBackendState {
  const game::InputLayout* layout = nullptr;
  HandlerCache cache = {};
};

InputBackendState g_state = {};

void SelectHandler(void* handler) noexcept {
  if (g_state.cache.handler == handler) {
    return;
  }
  g_state.cache = {};
  g_state.cache.handler = handler;
}

void* EventSink(void* handler) noexcept {
  SelectHandler(handler);
  if (handler == nullptr) {
    return nullptr;
  }
  if (g_state.cache.event_sink == nullptr) {
    const DWORD offset =
        g_state.layout != nullptr ? g_state.layout->input_to_event_sink : 0;
    if (offset != 0) {
      g_state.cache.event_sink = ReadPointer(Field(handler, offset));
    }
  }
  return g_state.cache.event_sink;
}

bool ResolveCursorFields(void* handler) noexcept {
  SelectHandler(handler);
  if (handler == nullptr) {
    return false;
  }
  if (g_state.cache.cursor_fields_resolved) {
    return g_state.cache.cursor_mode != nullptr && g_state.cache.cursor_command != nullptr;
  }

  g_state.cache.cursor_fields_resolved = true;
  if (g_state.layout == nullptr || g_state.layout->input_to_cursor_mode == 0 ||
      g_state.layout->input_to_cursor_command == 0) {
    return false;
  }

  auto* relative_field = Field(handler, g_state.layout->input_to_cursor_mode);
  auto* command_field = Field(handler, g_state.layout->input_to_cursor_command);
  if (::IsBadReadPtr(relative_field, 1) ||
      ::IsBadWritePtr(relative_field, 1) ||
      ::IsBadReadPtr(command_field, sizeof(DWORD)) ||
      ::IsBadWritePtr(command_field, sizeof(DWORD))) {
    return false;
  }

  g_state.cache.cursor_mode = relative_field;
  g_state.cache.cursor_command = command_field;
  return true;
}

}  // namespace

void BindLayout(const game::InputLayout* layout) noexcept {
  g_state.layout = layout;
  g_state.cache = {};
}

const game::InputLayout* BoundLayout() noexcept {
  return g_state.layout;
}

void* Handler(void* app_main) noexcept {
  if (app_main == nullptr || ::IsBadReadPtr(Field(app_main, 4), sizeof(void*))) {
    return nullptr;
  }

  void* platform = ReadPointer(Field(app_main, 4));
  if (platform == nullptr) {
    return nullptr;
  }

  if (g_state.layout == nullptr || g_state.layout->app_main_to_client == 0) {
    return nullptr;
  }

  void* slot = Field(platform, g_state.layout->app_main_to_client);
  if (g_state.layout->client_to_input != 0) {
    void* client = ReadPointer(slot);
    if (client == nullptr) {
      return nullptr;
    }
    slot = Field(client, g_state.layout->client_to_input);
  }

  return ReadPointer(slot);
}

bool PostPointer(void* handler, int x, int y, bool relative,
                 unsigned char action, unsigned char pressed) noexcept {
  if (handler == nullptr) {
    return false;
  }

  const game::InputLayout* const layout = g_state.layout;
  if (layout == nullptr) {
    return false;
  }
  void* sink = EventSink(handler);
  if (sink == nullptr) {
    return false;
  }

  if (relative) {
    if (x < -32768) x = -32768;
    if (x > 32767) x = 32767;
    if (y < -32768) y = -32768;
    if (y > 32767) y = 32767;
  } else {
    if (x < 0) x = 0;
    if (x > 65535) x = 65535;
    if (y < 0) y = 0;
    if (y > 65535) y = 65535;
  }

  // The event factory takes the first two arguments in ECX/EDX and leaves the
  // four remaining slots to the caller. Confirmed on the wire for 1.1.5, whose
  // factory at game+0x7AFEF0 ends in a bare `ret`; 0.15.10 shares this path and
  // is safe either way, because the release is computed from EBP rather than by
  // adding the pushed count back.
  using DispatchFn = void(__thiscall*)(void*, void*);

  void* const make_event = game::Resolve(layout->fn_make_mouse_event);
  const auto dispatch =
      GameFunction<DispatchFn>(layout->fn_dispatch_input_event);
  if (make_event == nullptr || dispatch == nullptr) {
    return false;
  }

  DWORD action_value = static_cast<DWORD>(action);
  DWORD pressed_value = static_cast<DWORD>(pressed);
  DWORD relative_value = relative ? 1u : 0u;
  short event_x = static_cast<short>(x);
  short event_y = static_cast<short>(y);
  void* event = nullptr;
  const void* const stack_arguments[] = {&pressed_value, &relative_value,
                                         &event_x, &event_y};
  hooks::x86::CallRegisterCallerClean(make_event, &event, &action_value,
                                      stack_arguments,
                                      CountOf(stack_arguments));

  if (event == nullptr || ::IsBadReadPtr(event, 0x14) ||
      *(static_cast<const unsigned char*>(event) + 4) != 0) {
    log::Write("mouse event factory returned an invalid event");
    return false;
  }

  dispatch(sink, event);
  return true;
}

bool ReadCursorState(void* handler, bool* relative, int* command) noexcept {
  if (relative == nullptr || command == nullptr ||
      !ResolveCursorFields(handler)) {
    return false;
  }

  *relative = *g_state.cache.cursor_mode != 0;
  *command = *reinterpret_cast<const int*>(g_state.cache.cursor_command);
  return true;
}

void WriteCursorMode(void* handler, bool relative) noexcept {
  if (!ResolveCursorFields(handler)) {
    return;
  }

  *g_state.cache.cursor_mode = relative ? 1 : 0;
  *reinterpret_cast<DWORD*>(g_state.cache.cursor_command) = 0;
}

unsigned char* CursorToggle(void* handler) noexcept {
  return ResolveCursorFields(handler) ? g_state.cache.cursor_mode : nullptr;
}

void WriteCursorCommand(void* handler, unsigned int command) noexcept {
  if (!ResolveCursorFields(handler)) {
    return;
  }
  *reinterpret_cast<DWORD*>(g_state.cache.cursor_command) = static_cast<DWORD>(command);
}

}  // namespace shim::versions::modern::input_backend
