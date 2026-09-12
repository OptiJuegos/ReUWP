#include "shim/version_input.h"

#include "shim/game_profile.h"
#include "shim/input.h"
#include "shim/input_backend.h"
#include "shim/log.h"
#include "shim/version_asm.h"
#include "shim/x86_patch.h"

namespace shim::versions::v0132::input_backend {
namespace {

using shim::input::backend::Field;
using shim::input::backend::GameFunction;
using shim::input::backend::ReadPointer;

void* g_app_main = nullptr;
void* g_cached_platform = nullptr;
unsigned char* g_cached_state = nullptr;

void* Platform() noexcept {
  if (g_cached_platform != nullptr) {
    return g_cached_platform;
  }
  if (g_app_main == nullptr) {
    return nullptr;
  }
  const game::Version0132Layout& layout = game::Layout0132();
  g_cached_platform =
      ReadPointer(Field(g_app_main, layout.app_main_platform_offset));
  return g_cached_platform;
}

unsigned char* State(void* platform = nullptr) noexcept {
  if (platform == nullptr && g_cached_state != nullptr) {
    return g_cached_state;
  }
  if (platform == nullptr) {
    platform = Platform();
  }
  if (platform == nullptr) {
    return nullptr;
  }
  const game::Version0132Layout& layout = game::Layout0132();
  auto* state = static_cast<unsigned char*>(
      ReadPointer(Field(platform, layout.platform_state_offset)));
  if (platform == g_cached_platform) {
    g_cached_state = state;
  }
  return state;
}

#pragma pack(push, 1)
struct MouseEvent {
  unsigned char type;
  unsigned char reserved_01;
  short x;
  short y;
  unsigned short reserved_06;
  DWORD relative;
  unsigned char action;
  unsigned char pressed;
  unsigned short reserved_0e;
};

struct KeyEvent {
  unsigned char type;
  unsigned char key;
  unsigned short reserved_02;
  DWORD state;
};
#pragma pack(pop)

static_assert(sizeof(MouseEvent) == 0x10,
              "Minecraft 0.13.2 mouse event ABI changed");
static_assert(offsetof(MouseEvent, x) == 0x02,
              "Minecraft 0.13.2 mouse x offset changed");
static_assert(offsetof(MouseEvent, y) == 0x04,
              "Minecraft 0.13.2 mouse y offset changed");
static_assert(offsetof(MouseEvent, relative) == 0x08,
              "Minecraft 0.13.2 mouse relative offset changed");
static_assert(offsetof(MouseEvent, action) == 0x0C,
              "Minecraft 0.13.2 mouse action offset changed");
static_assert(offsetof(MouseEvent, pressed) == 0x0D,
              "Minecraft 0.13.2 mouse pressed offset changed");
static_assert(sizeof(KeyEvent) == 0x08,
              "Minecraft 0.13.2 key event ABI changed");
static_assert(offsetof(KeyEvent, state) == 0x04,
              "Minecraft 0.13.2 key state offset changed");

void* AllocateEvent(size_t size) noexcept {
  using AllocFn = void*(__cdecl*)(size_t);
  const auto allocate =
      GameFunction<AllocFn>(game::Layout0132().fn_input_event_alloc);
  return allocate != nullptr ? allocate(size) : nullptr;
}

bool DispatchEvent(void* event) noexcept {
  void* platform = Platform();
  if (platform == nullptr || event == nullptr) {
    return false;
  }
  using DispatchFn = void(__thiscall*)(void*, void*);
  const auto dispatch =
      GameFunction<DispatchFn>(game::Layout0132().fn_input_dispatch);
  if (dispatch == nullptr) {
    return false;
  }
  dispatch(platform, event);
  return true;
}

int TranslateKey(unsigned int virtual_key) noexcept {
  using TranslateFn = int(__fastcall*)(void*, unsigned int);
  const auto translate =
      GameFunction<TranslateFn>(game::Layout0132().fn_translate_key);
  return translate != nullptr ? translate(nullptr, virtual_key) : 0;
}

void WriteCursorCommand(void* state, DWORD command) noexcept {
  if (state == nullptr) {
    return;
  }
  auto* address =
      Field(state, game::Layout0132().state_cursor_command_offset);
  if (!::IsBadWritePtr(address, sizeof(DWORD))) {
    *reinterpret_cast<DWORD*>(address) = command;
  }
}

void __fastcall MouseModeRelativeHook(void* platform, void*) noexcept {
  unsigned char* state = State(platform);
  if (input::CursorGuardActive()) {
    input::RequestCursorMode(false);
    WriteCursorCommand(state, 0);
    input::InvalidateCursorClip();
    return;
  }
  if (state == nullptr) {
    return;
  }

  unsigned char* toggle =
      Field(state, game::Layout0132().state_cursor_toggle_offset);
  if (!::IsBadReadPtr(toggle, 1) && *toggle == 0) {
    input::RequestCursorMode(true);
    WriteCursorCommand(state, 1);
    input::InvalidateCursorClip();
  }
}

void __fastcall MouseModeReleaseHook(void* platform, void*) noexcept {
  WriteCursorCommand(State(platform), 2);
  input::ReleaseCursorGuard();
}

void __fastcall MouseModeToggleHook(void* platform, void*) noexcept {
  unsigned char* state = State(platform);
  if (state == nullptr) {
    return;
  }
  const game::Version0132Layout& layout = game::Layout0132();
  if (input::CursorGuardActive()) {
    WriteCursorCommand(state, 0);
    input::RequestCursorMode(false);
  } else {
    unsigned char* toggle = Field(state, layout.state_cursor_toggle_offset);
    if (::IsBadWritePtr(toggle, 1)) {
      return;
    }
    const bool old_toggle = *toggle != 0;
    *toggle = old_toggle ? 0 : 1;
    input::RequestCursorMode(old_toggle);
    WriteCursorCommand(state, old_toggle ? 1u : 2u);
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

const game::InputPatchCatalog* PatchCatalog() noexcept {
  const game::VersionProfile* profile = game::CurrentProfile();
  return profile != nullptr ? profile->input_patches : nullptr;
}

}  // namespace

void SetAppMain(void* app_main) noexcept {
  g_app_main = app_main;
  g_cached_platform = nullptr;
  g_cached_state = nullptr;
}

void* Handler() noexcept {
  return Platform();
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
       "0.13.2 mouse-mode hook #0 failed"},
      {catalog->mouse_release.rva, catalog->mouse_release.expected,
       static_cast<uint32_t>(catalog->mouse_release.size),
       reinterpret_cast<const void*>(&MouseModeReleaseHook),
       hooks::x86::RelativeBranch::kJump,
       "0.13.2 mouse-mode hook #1 failed"},
      {catalog->mouse_toggle.rva, catalog->mouse_toggle.expected,
       static_cast<uint32_t>(catalog->mouse_toggle.size),
       reinterpret_cast<const void*>(&MouseModeToggleHook),
       hooks::x86::RelativeBranch::kJump,
       "0.13.2 mouse-mode hook #2 failed"},
      {catalog->text_show.rva, catalog->text_show.expected,
       static_cast<uint32_t>(catalog->text_show.size),
       reinterpret_cast<const void*>(&TextShowHook),
       hooks::x86::RelativeBranch::kJump,
       "0.13.2 text-show hook failed"},
      {catalog->text_hide.rva, catalog->text_hide.expected,
       static_cast<uint32_t>(catalog->text_hide.size),
       reinterpret_cast<const void*>(&TextHideHook),
       hooks::x86::RelativeBranch::kJump,
       "0.13.2 text-hide hook failed"},
  };

  const bool ok = hooks::x86::ApplyRelativeBranchPatches(
      reinterpret_cast<uintptr_t>(game::Base()), patches, CountOf(patches));
  if (ok) {
    log::Write("0.13.2 Win32 input hooks installed");
  }
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
  if (Platform() == nullptr) {
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

  auto* event = static_cast<MouseEvent*>(AllocateEvent(sizeof(MouseEvent)));
  if (event == nullptr) {
    return false;
  }
  memset(event, 0, sizeof(*event));
  event->x = static_cast<short>(x);
  event->y = static_cast<short>(y);
  event->relative = relative ? 1u : 0u;
  event->action = action;
  event->pressed = pressed;
  return DispatchEvent(event);
}

bool PostKey(unsigned int virtual_key, bool down) noexcept {
  if (Platform() == nullptr) {
    return false;
  }

  const int game_key = TranslateKey(virtual_key);
  if (game_key <= 0 || game_key > 255) {
    return false;
  }

  auto* event = static_cast<KeyEvent*>(AllocateEvent(sizeof(KeyEvent)));
  if (event == nullptr) {
    return false;
  }
  memset(event, 0, sizeof(*event));
  event->type = 2;
  event->key = static_cast<unsigned char>(game_key);
  event->state = down ? 1u : 0u;
  return DispatchEvent(event);
}

bool PostChar(unsigned int codepoint) noexcept {
  if (Platform() == nullptr) {
    return false;
  }

  char utf8[5] = {};
  input::backend::EncodeUtf8(codepoint, utf8);
  void* factory = game::Resolve(game::Layout0132().fn_make_text_event);
  if (factory == nullptr) {
    return false;
  }

  void* event = asm_hooks::CallTextFactory(factory, utf8);
  if (event == nullptr || ::IsBadReadPtr(event, 0x20) ||
      *static_cast<const unsigned char*>(event) != 3) {
    log::Write("0.13.2 text event factory returned an invalid event");
    return false;
  }
  return DispatchEvent(event);
}

bool ReadCursorState(bool* relative, int* command) noexcept {
  if (relative == nullptr || command == nullptr) {
    return false;
  }
  unsigned char* state = State();
  if (state == nullptr) {
    return false;
  }

  const game::Version0132Layout& layout = game::Layout0132();
  unsigned char* relative_field = Field(state, layout.state_relative_offset);
  unsigned char* command_field = Field(state, layout.state_cursor_command_offset);
  if (::IsBadReadPtr(relative_field, 1) ||
      ::IsBadReadPtr(command_field, sizeof(DWORD))) {
    return false;
  }

  *relative = *relative_field != 0;
  *command = *reinterpret_cast<const int*>(command_field);
  return true;
}

void WriteCursorMode(bool relative) noexcept {
  unsigned char* state = State();
  if (state == nullptr) {
    return;
  }
  unsigned char* field = Field(state, game::Layout0132().state_relative_offset);
  if (!::IsBadWritePtr(field, 1)) {
    *field = relative ? 1 : 0;
  }
  WriteCursorCommand(state, 0);
}

}  // namespace shim::versions::v0132::input_backend
