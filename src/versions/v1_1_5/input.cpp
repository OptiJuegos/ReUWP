#include "shim/version_input.h"

#include "shim/game_profile.h"
#include "shim/input.h"
#include "shim/input_backend.h"
#include "shim/log.h"
#include "shim/modern_input.h"
#include "shim/x86_abi.h"
#include "shim/x86_patch.h"

namespace shim::versions::v115::input_backend {
namespace {

using shim::input::backend::Field;
using shim::input::backend::GameFunction;
using shim::input::backend::ReadPointer;

void* g_app_main = nullptr;
void* g_cached_handler = nullptr;

const game::InputPatchCatalog* PatchCatalog() noexcept {
  const game::VersionProfile* profile = game::CurrentProfile();
  return profile != nullptr ? profile->input_patches : nullptr;
}

void WriteCursorCommand(DWORD command) noexcept {
  modern::input_backend::WriteCursorCommand(Handler(), command);
}

void __fastcall MouseModeRelativeHook(void*, void*) noexcept {
  unsigned char* toggle = modern::input_backend::CursorToggle(Handler());
  if (input::CursorGuardActive()) {
    input::RequestCursorMode(false);
    WriteCursorCommand(0);
    input::InvalidateCursorClip();
    return;
  }
  if (toggle != nullptr && *toggle == 0) {
    input::RequestCursorMode(true);
    WriteCursorCommand(1);
    input::InvalidateCursorClip();
  }
}

void __fastcall MouseModeReleaseHook(void*, void*) noexcept {
  WriteCursorCommand(2);
  input::ReleaseCursorGuard();
}

void __fastcall MouseModeToggleHook(void*, void*) noexcept {
  unsigned char* toggle = modern::input_backend::CursorToggle(Handler());
  if (toggle == nullptr) {
    return;
  }
  if (input::CursorGuardActive()) {
    WriteCursorCommand(0);
    input::RequestCursorMode(false);
  } else {
    const bool old_toggle = *toggle != 0;
    *toggle = old_toggle ? 0 : 1;
    input::RequestCursorMode(old_toggle);
    WriteCursorCommand(old_toggle ? 1u : 2u);
  }
  input::InvalidateCursorClip();
}

void __fastcall TextShowHook(void*, void*) noexcept {
  input::backend::ActivateTextInput();
}

void __fastcall TextHideHook(void*, void*) noexcept {
  input::backend::DeactivateTextInput();
}

}  // namespace

void SetAppMain(void* app_main) noexcept {
  g_app_main = app_main;
  g_cached_handler = nullptr;
}

void* Handler() noexcept {
  if (g_cached_handler == nullptr) {
    g_cached_handler = modern::input_backend::Handler(g_app_main);
  }
  return g_cached_handler;
}

bool InstallHooks() noexcept {
  const game::InputPatchCatalog* catalog = PatchCatalog();
  if (catalog == nullptr) {
    return false;
  }

  const hooks::x86::RelativeBranchPatch patches[] = {
      {catalog->mouse_relative.rva, catalog->mouse_relative.expected,
       static_cast<uint32_t>(catalog->mouse_relative.size),
       reinterpret_cast<const void*>(&MouseModeRelativeHook),
       hooks::x86::RelativeBranch::kJump,
       "1.1.5 mouse-relative hook failed"},
      {catalog->mouse_release.rva, catalog->mouse_release.expected,
       static_cast<uint32_t>(catalog->mouse_release.size),
       reinterpret_cast<const void*>(&MouseModeReleaseHook),
       hooks::x86::RelativeBranch::kJump,
       "1.1.5 mouse-release hook failed"},
      {catalog->mouse_toggle.rva, catalog->mouse_toggle.expected,
       static_cast<uint32_t>(catalog->mouse_toggle.size),
       reinterpret_cast<const void*>(&MouseModeToggleHook),
       hooks::x86::RelativeBranch::kJump,
       "1.1.5 mouse-toggle hook failed"},
      {catalog->text_show.rva, catalog->text_show.expected,
       static_cast<uint32_t>(catalog->text_show.size),
       reinterpret_cast<const void*>(&TextShowHook),
       hooks::x86::RelativeBranch::kJump,
       "1.1.5 text-show hook failed"},
      {catalog->text_hide.rva, catalog->text_hide.expected,
       static_cast<uint32_t>(catalog->text_hide.size),
       reinterpret_cast<const void*>(&TextHideHook),
       hooks::x86::RelativeBranch::kJump,
       "1.1.5 text-hide hook failed"},
  };

  const bool ok = hooks::x86::ApplyRelativeBranchPatches(
      reinterpret_cast<uintptr_t>(game::Base()), patches, CountOf(patches));
  log::Write(ok ? "Minecraft 1.1.5 HWND mouse/text hooks installed"
                : "Minecraft 1.1.5 HWND mouse/text hooks unavailable");
  return ok;
}

bool InstallLifecycleHooks() noexcept {
  return true;
}

bool RequestSubmit() noexcept {
  return false;
}

void PumpSubmit() noexcept {}

bool PostPointer(int x, int y, bool relative, unsigned char action,
                 unsigned char pressed) noexcept {
  return modern::input_backend::PostPointer(Handler(), x, y, relative, action,
                                            pressed);
}

bool PostKey(unsigned int virtual_key, bool down) noexcept {
  void* handler = Handler();
  if (handler == nullptr) {
    return false;
  }

  const unsigned char folded = static_cast<unsigned char>(virtual_key + 33);
  if (folded < 41) {
    return false;
  }

  const game::InputLayout* const layout = modern::input_backend::BoundLayout();
  if (layout == nullptr) {
    return false;
  }
  using TranslateFn = unsigned char(__fastcall*)(unsigned int key,
                                                  void* unused_edx);
  using DispatchFn = void(__thiscall*)(void*, void*);

  const auto translate =
      GameFunction<TranslateFn>(layout->fn_translate_key);
  // Register pair plus a caller-released tail, like the other event factories.
  void* const make_event = game::Resolve(layout->fn_make_key_event);
  const auto dispatch =
      GameFunction<DispatchFn>(layout->fn_dispatch_input_event);
  if (translate == nullptr || make_event == nullptr || dispatch == nullptr) {
    return false;
  }

  unsigned char game_key = translate(virtual_key - 8, nullptr);
  if (game_key == 0) {
    return false;
  }

  DWORD down_value = down ? 1u : 0u;
  void* event = nullptr;
  const void* const stack_arguments[] = {&down_value};
  hooks::x86::CallRegisterCallerClean(make_event, &event, &game_key,
                                      stack_arguments,
                                      CountOf(stack_arguments));
  if (event == nullptr || ::IsBadReadPtr(event, 0x10) ||
      *(static_cast<const unsigned char*>(event) + 4) != 2) {
    log::Write("key event factory returned an invalid event");
    return false;
  }

  void* sink = ReadPointer(Field(handler, layout->input_to_event_sink));
  if (sink == nullptr) {
    return false;
  }
  dispatch(sink, event);

  if (layout->input_to_key_states != 0) {
    *Field(handler, layout->input_to_key_states + static_cast<DWORD>(game_key)) =
        down ? 1 : 0;
  }
  return true;
}

bool PostChar(unsigned int codepoint) noexcept {
  void* handler = Handler();
  if (handler == nullptr) {
    return false;
  }

  const game::InputLayout* const layout = modern::input_backend::BoundLayout();
  if (layout == nullptr) {
    return false;
  }
  void* sink = ReadPointer(Field(handler, layout->input_to_event_sink));
  if (sink == nullptr) {
    return false;
  }

  char utf8[5] = {};
  const int length = input::backend::EncodeUtf8(codepoint, utf8);

  struct GameString {
    char buffer[16];
    unsigned int size;
    unsigned int capacity;
  };
  static_assert(sizeof(GameString) == 24,
                "MSVC small-string layout must stay 24 bytes");

  GameString text = {};
  memcpy(text.buffer, utf8, static_cast<size_t>(length));
  text.size = static_cast<unsigned int>(length);
  text.capacity = 15;

  unsigned char* counter = Field(handler, layout->input_to_text_sequence);
  unsigned char sequence = *counter;
  *counter = static_cast<unsigned char>(sequence + 1);
  unsigned char zero = 0;
  void* event = nullptr;

  using DispatchFn = void(__thiscall*)(void*, void*);
  // Register pair plus a caller-released tail, like the other event factories.
  void* const make_event = game::Resolve(layout->fn_make_text_event);
  const auto dispatch =
      GameFunction<DispatchFn>(layout->fn_dispatch_input_event);
  if (make_event == nullptr || dispatch == nullptr) {
    return false;
  }

  const void* const stack_arguments[] = {&zero, &sequence};
  hooks::x86::CallRegisterCallerClean(make_event, &event, &text,
                                      stack_arguments,
                                      CountOf(stack_arguments));
  if (event == nullptr || ::IsBadReadPtr(event, 0x24) ||
      *(static_cast<const unsigned char*>(event) + 4) != 3) {
    log::Write("text event factory returned an invalid event");
    return false;
  }

  dispatch(sink, event);
  return true;
}

bool ReadCursorState(bool* relative, int* command) noexcept {
  return modern::input_backend::ReadCursorState(Handler(), relative, command);
}

void WriteCursorMode(bool relative) noexcept {
  modern::input_backend::WriteCursorMode(Handler(), relative);
}

}  // namespace shim::versions::v115::input_backend
