#pragma once

#include "shim/game_layout.h"

namespace shim::game {

enum class VersionCapability : unsigned int {
  kNone = 0,
  kDrainCoreDispatcher = 1u << 4,
  kResourceActivation = 1u << 6,
  kStoragePickerActivation = 1u << 7,
  kWin7SpeechActivation = 1u << 8,
  kPreD3DCompatibility = 1u << 9,
  kStdioOverrides = 1u << 10,
  kRetainD3DDevicePair = 1u << 11,
  kRenderer0132Layout = 1u << 13,
  kAsyncInfoProjection = 1u << 14,
  kStorageV2Projection = 1u << 15,
  kAppMain115Diagnostics = 1u << 16,
};

constexpr VersionCapability operator|(VersionCapability left,
                                      VersionCapability right) noexcept {
  return static_cast<VersionCapability>(
      static_cast<unsigned int>(left) | static_cast<unsigned int>(right));
}

constexpr bool HasCapability(VersionCapability value,
                             VersionCapability capability) noexcept {
  return (static_cast<unsigned int>(value) &
          static_cast<unsigned int>(capability)) != 0;
}

struct CodeSignatureSite {
  DWORD rva;
  unsigned char expected[10];
  size_t size;
};

struct InputPatchCatalog {
  CodeSignatureSite mouse_relative;
  CodeSignatureSite mouse_release;
  CodeSignatureSite mouse_toggle;
  CodeSignatureSite text_show;
  CodeSignatureSite text_hide;

  DWORD chat_screen_vtable_rva;
  DWORD chat_screen_open_slot_rva;
  DWORD chat_screen_close_slot_rva;
  DWORD chat_screen_expected_open_rva;
  DWORD chat_screen_expected_close_rva;
  DWORD chat_submit_rva;
};


using DedicatedRuntimeFn = WPARAM (*)() noexcept;
using RuntimeBringUpFn = bool (*)() noexcept;
using RuntimeFrameFn = void (*)() noexcept;

struct RuntimeOperations {
  DedicatedRuntimeFn run;
  RuntimeBringUpFn bring_up;
  RuntimeFrameFn run_frame;
};

using StartupPatchFn = bool (*)() noexcept;
using StartupPostWindowFn = void (*)() noexcept;

struct StartupOperations {
  StartupPatchFn install_before_common_compatibility;
  StartupPatchFn install_after_common_compatibility;
  StartupPatchFn install_pre_d3d;
  StartupPostWindowFn install_post_window;
};

using FmodInstallFn = bool (*)() noexcept;

struct FmodOperations {
  FmodInstallFn install;
};


using InputSetAppMainFn = void (*)(void*) noexcept;
using InputHandlerFn = void* (*)() noexcept;
using InputInstallFn = bool (*)() noexcept;
using InputRequestSubmitFn = bool (*)() noexcept;
using InputPumpSubmitFn = void (*)() noexcept;
using InputPostPointerFn = bool (*)(int, int, bool, unsigned char,
                                    unsigned char) noexcept;
using InputPostKeyFn = bool (*)(unsigned int, bool) noexcept;
using InputPostCharFn = bool (*)(unsigned int) noexcept;
using InputReadCursorStateFn = bool (*)(bool*, int*) noexcept;
using InputWriteCursorModeFn = void (*)(bool) noexcept;

struct InputOperations {
  InputSetAppMainFn set_app_main;
  InputHandlerFn handler;
  InputInstallFn install_hooks;
  InputInstallFn install_lifecycle_hooks;
  InputRequestSubmitFn request_submit;
  InputPumpSubmitFn pump_submit;
  InputPostPointerFn post_pointer;
  InputPostKeyFn post_key;
  InputPostCharFn post_char;
  InputReadCursorStateFn read_cursor_state;
  InputWriteCursorModeFn write_cursor_mode;
};

struct StdioPatchCatalog {
  DWORD fopen_iat_rva;
  DWORD wfopen_s_iat_rva;
  DWORD wfopen_iat_rva;
  DWORD vfscanf_iat_rva;
  DWORD vsscanf_iat_rva;
};

struct VersionProfile {
  Version version;

  // Build identity. `time_date_stamp` comes from Mojang's build and is the same
  // before and after patching; `image_size` moves with the patch geometry, so
  // it identifies the patcher as much as the game and is only a fallback for
  // builds whose stamp has not been recorded yet. Zero means "not known".
  DWORD time_date_stamp;
  DWORD image_size;
  const char* version_name;
  const wchar_t* window_class_name;
  const wchar_t* window_title;
  const Layout* layout;
  const Version0132Layout* layout0132;
  const FmodOperations* fmod;
  const InputPatchCatalog* input_patches;
  const InputOperations* input;
  const StdioPatchCatalog* stdio_patches;
  const StartupOperations* startup;
  const RuntimeOperations* runtime;
  VersionCapability capabilities;
  const CodeSignatureSite* identity_signatures = nullptr;
  size_t identity_signature_count = 0;
};

// Finds a supported build descriptor from the loaded PE image without changing
// process-global state.
const VersionProfile* DetectProfile(HMODULE module) noexcept;

// Returns the descriptor selected by Initialize(), or nullptr before
// initialization and for unsupported builds.
const VersionProfile* CurrentProfile() noexcept;

// Human-readable version name for the active profile. Unknown builds return
// "unknown".
const char* VersionName() noexcept;

// Tests one behavior capability on the active version profile.
bool HasCapability(VersionCapability capability) noexcept;

}  // namespace shim::game
