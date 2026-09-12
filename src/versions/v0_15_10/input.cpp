#include "shim/version_input.h"

#include "shim/game_profile.h"
#include "shim/input.h"
#include "shim/input_backend.h"
#include "shim/log.h"
#include "shim/modern_input.h"
#include "shim/patch.h"
#include "shim/version_asm.h"
#include "shim/x86_patch.h"

namespace shim::versions::v01510::input_backend {
namespace {

using shim::input::backend::Field;
using shim::input::backend::GameFunction;
using shim::input::backend::ReadPointer;

void* g_app_main = nullptr;
void* g_cached_handler = nullptr;
void* g_active_chat_screen = nullptr;
void* g_original_chat_screen_open = nullptr;
void* g_original_chat_screen_close = nullptr;
volatile LONG g_chat_submit_requested = 0;

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

void __fastcall TextShowHook(void* self, void*, void*, void*, void*, void*,
                             void*) noexcept {
  if (self != nullptr && !::IsBadWritePtr(Field(self, 9), 1)) {
    *Field(self, 9) = 1;
  }
  input::backend::ActivateTextInput();
}

void __fastcall TextHideHook(void* self, void*) noexcept {
  if (self != nullptr && !::IsBadWritePtr(Field(self, 9), 1)) {
    *Field(self, 9) = 0;
  }
  input::backend::DeactivateTextInput();
}

void __fastcall ChatScreenCloseHook(void* self, void*) noexcept {
  using CloseFn = void(__thiscall*)(void*);
  if (g_original_chat_screen_close != nullptr) {
    reinterpret_cast<CloseFn>(g_original_chat_screen_close)(self);
  }

  if (g_active_chat_screen == self) {
    g_active_chat_screen = nullptr;
    input::backend::DeactivateTextInput();
  }
  ::InterlockedExchange(&g_chat_submit_requested, 0);
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
       "0.15.10 mouse-relative hook failed"},
      {catalog->mouse_release.rva, catalog->mouse_release.expected,
       static_cast<uint32_t>(catalog->mouse_release.size),
       reinterpret_cast<const void*>(&MouseModeReleaseHook),
       hooks::x86::RelativeBranch::kJump,
       "0.15.10 mouse-release hook failed"},
      {catalog->mouse_toggle.rva, catalog->mouse_toggle.expected,
       static_cast<uint32_t>(catalog->mouse_toggle.size),
       reinterpret_cast<const void*>(&MouseModeToggleHook),
       hooks::x86::RelativeBranch::kJump,
       "0.15.10 mouse-toggle hook failed"},
      {catalog->text_show.rva, catalog->text_show.expected,
       static_cast<uint32_t>(catalog->text_show.size),
       reinterpret_cast<const void*>(&TextShowHook),
       hooks::x86::RelativeBranch::kJump,
       "0.15.10 text-show hook failed"},
      {catalog->text_hide.rva, catalog->text_hide.expected,
       static_cast<uint32_t>(catalog->text_hide.size),
       reinterpret_cast<const void*>(&TextHideHook),
       hooks::x86::RelativeBranch::kJump,
       "0.15.10 text-hide hook failed"},
  };

  const bool ok = hooks::x86::ApplyRelativeBranchPatches(
      reinterpret_cast<uintptr_t>(game::Base()), patches, CountOf(patches));
  log::Write(ok ? "Minecraft 0.15.10 HWND mouse/text hooks installed"
                : "Minecraft 0.15.10 HWND mouse/text hooks unavailable");
  return ok;
}

bool InstallLifecycleHooks() noexcept {
#if !defined(_MSC_VER) || !defined(_M_IX86)
  log::Write("0.15.10 ChatScreen hooks require x86 MSVC naked trampoline");
  return false;
#else
  const game::InputPatchCatalog* catalog = PatchCatalog();
  if (catalog == nullptr) {
    log::Write("0.15.10 ChatScreen patch catalog unavailable");
    return false;
  }

  auto** open_slot =
      reinterpret_cast<void**>(game::Resolve(catalog->chat_screen_open_slot_rva));
  auto** close_slot = reinterpret_cast<void**>(
      game::Resolve(catalog->chat_screen_close_slot_rva));
  if (open_slot == nullptr || close_slot == nullptr ||
      ::IsBadReadPtr(open_slot, sizeof(void*)) ||
      ::IsBadReadPtr(close_slot, sizeof(void*))) {
    log::Write("0.15.10 ChatScreen vtable slots unavailable");
    return false;
  }

  void* expected_open = game::Resolve(catalog->chat_screen_expected_open_rva);
  void* expected_close = game::Resolve(catalog->chat_screen_expected_close_rva);
  if (expected_open == nullptr || expected_close == nullptr ||
      *open_slot != expected_open || *close_slot != expected_close) {
    log::Write("Minecraft 0.15.10 ChatScreen vtable signature mismatch");
    return false;
  }

  g_original_chat_screen_open = nullptr;
  g_original_chat_screen_close = nullptr;
  const void* open_hook = asm_hooks::ChatScreenOpenEntry();
  if (open_hook == nullptr) {
    log::Write("0.15.10 ChatScreen open trampoline unavailable");
    return false;
  }
  asm_hooks::ConfigureChatScreenOpen(&g_active_chat_screen,
                                     input::backend::TextInputActiveFlag(),
                                     expected_open);
  if (!hooks::ReplacePointer(open_slot, open_hook, &g_original_chat_screen_open,
                             "0.15.10 ChatScreen open")) {
    return false;
  }
  if (!hooks::ReplacePointer(close_slot,
                             reinterpret_cast<const void*>(&ChatScreenCloseHook),
                             &g_original_chat_screen_close,
                             "0.15.10 ChatScreen close")) {
    void* ignored = nullptr;
    hooks::ReplacePointer(open_slot, g_original_chat_screen_open, &ignored,
                          "0.15.10 ChatScreen open rollback");
    g_original_chat_screen_open = nullptr;
    return false;
  }

  ::InterlockedExchange(&g_chat_submit_requested, 0);
  log::Write("0.15.10 ChatScreen lifecycle hooks installed");
  return true;
#endif
}

bool RequestSubmit() noexcept {
  const game::InputPatchCatalog* catalog = PatchCatalog();
  void* screen = g_active_chat_screen;
  if (catalog == nullptr || screen == nullptr ||
      ::IsBadReadPtr(screen, sizeof(void*))) {
    log::Write("Minecraft 0.15.10 Enter ignored: active ChatScreen unavailable");
    return false;
  }

  void* expected_vtable = game::Resolve(catalog->chat_screen_vtable_rva);
  if (*reinterpret_cast<void**>(screen) != expected_vtable) {
    log::Write("Minecraft 0.15.10 Enter ignored: active ChatScreen unavailable");
    return false;
  }

  ::InterlockedExchange(&g_chat_submit_requested, 1);
  return true;
}

void PumpSubmit() noexcept {
  if (::InterlockedExchange(&g_chat_submit_requested, 0) == 0) {
    return;
  }

  const game::InputPatchCatalog* catalog = PatchCatalog();
  void* screen = g_active_chat_screen;
  if (catalog == nullptr || screen == nullptr ||
      ::IsBadReadPtr(screen, sizeof(void*)) ||
      *reinterpret_cast<void**>(screen) !=
          game::Resolve(catalog->chat_screen_vtable_rva)) {
    return;
  }

  using SubmitFn = void(__thiscall*)(void*);
  if (const auto submit = GameFunction<SubmitFn>(catalog->chat_submit_rva)) {
    submit(screen);
  }
}

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

  const game::InputLayout* const layout = modern::input_backend::BoundLayout();
  if (layout == nullptr) {
    return false;
  }
  using KeyFn = void(__fastcall*)(void*, unsigned int);
  const DWORD rva =
      layout->fn_key_down +
      (down ? 0 : game::InputLayout::kKeyUpDelta);
  if (const auto key_fn = GameFunction<KeyFn>(rva)) {
    key_fn(handler, virtual_key);
    return true;
  }
  return false;
}

bool PostChar(unsigned int codepoint) noexcept {
  if (codepoint == 13) {
    return false;
  }

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
  input::backend::EncodeUtf8(codepoint, utf8);
  unsigned char zero = 0;
  void* event = nullptr;

  using MakeTextEventFn = void(__fastcall*)(void** out_event, char* text,
                                            unsigned char* zero);
  using DispatchFn = void(__thiscall*)(void*, void*);
  const auto make_event =
      GameFunction<MakeTextEventFn>(layout->fn_make_text_event);
  const auto dispatch =
      GameFunction<DispatchFn>(layout->fn_dispatch_input_event);
  if (make_event == nullptr || dispatch == nullptr) {
    return false;
  }

  make_event(&event, utf8, &zero);
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

}  // namespace shim::versions::v01510::input_backend
