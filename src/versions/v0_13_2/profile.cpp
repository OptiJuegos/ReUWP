#include "shim/game_profiles.h"

#include "shim/branding.h"
#include "shim/version_fmod.h"
#include "shim/version_runtime.h"
#include "shim/version_input.h"

namespace shim::game {
namespace {

// Minecraft 0.13.2 (SizeOfImage 0x008CC000).
//
// The disassembly corrects an IDA boundary error: this branch starts at
// 0x629557E0 (FUN_629557e0 in Ghidra), not 0x629557C0. The latter address is
// inside the previous function.
//
// This data intentionally uses Version0132Layout because bring-up has a
// different ABI: it allocates a 0x1c-byte owner first, obtains renderer/device
// from it, and constructs DX::DeviceResources and MCPE_Host::AppMain later.
constexpr Version0132Layout kLayout0132 = {
    /* iat_activation_factory       */ 0x0060F980,
    /* iat_d3d_compile              */ 0x0060F028,
    /* iat_d3d11_create_device      */ 0x0060F880,
    /* iat_frame_sleep              */ 0x0060F30C,
    /* iid_d3d11_device2            */ 0x0067F56C,
    /* iid_d3d11_context2           */ 0x0067F55C,

    /* fmod_dll_name                */ 0x006108F0,
    /* fmod_delay_iat               */ 0x007E09CC,
    /* fmod_bridge_slots            */ 25,

    /* custom_skin_hook             */ 0x003C14D0,
    /* fullscreen_hook              */ 0x003C26E0,
    /* help_url_hook                */ 0x000C3CF0,
    /* achievements_iat             */ 0x0060F870,
    /* invite_player_iat            */ 0x0060F878,

    /* game_malloc_iat              */ 0x0060F694,
    /* fn_resource_owner_ctor       */ 0x005366E0,
    /* resource_owner_slot          */ 0x0078C6C4,
    /* fn_game_d3d_init             */ 0x005391D0,
    /* fn_init_render_targets       */ 0x00538F90,
    /* fn_release_render_targets    */ 0x005398F0,
    /* fn_device_resources_ctor     */ 0x003EA440,
    /* fn_app_main_ctor             */ 0x003EA3B0,
    /* app_platform_slot            */ 0x007E0EE0,
    /* fn_app_main_frame            */ 0x003BD630,

    /* fn_input_event_alloc         */ 0x005798EA,
    /* fn_input_dispatch            */ 0x003C10E0,
    /* fn_translate_key             */ 0x00214CF0,
    /* fn_make_text_event           */ 0x00219820,

    /* owner_renderer_offset        */ 0x18,
    /* owner_device_offset          */ 0x10,
    /* renderer_context_offset      */ 0x84,
    /* renderer_swap_chain_offset   */ 0x74,
    /* renderer_rtv_offset          */ 0x88,
    /* renderer_width_offset        */ 0x60,
    /* renderer_height_offset       */ 0x64,
    /* renderer_depth_stencil_offset*/ 0x90,
    /* renderer_viewport_offset     */ 0x94,

    /* app_main_platform_offset     */ 0x04,
    /* app_main_game_offset         */ 0x08,
    /* app_main_frame_enabled_offset*/ 0x10,

    /* platform_state_offset          */ 0x13C,
    /* platform_width_offset          */ 0x140,
    /* platform_height_offset         */ 0x144,
    /* platform_listener_list_offset  */ 0x100,

    /* state_relative_offset          */ 0x45,
    /* state_cursor_command_offset    */ 0x48,
    /* state_cursor_toggle_offset     */ 0x6C,
};

constexpr Layout kEmptyLayout = {};


constexpr InputPatchCatalog kInputPatches = {
    {0x003C0C90,
     {0x8B, 0x81, 0x3C, 0x01, 0x00, 0x00, 0x80, 0x78, 0x6C, 0x00}, 10},
    {0x003C0CB0,
     {0x8B, 0x81, 0x3C, 0x01, 0x00, 0x00, 0xC7, 0x40, 0x48, 0x02}, 10},
    {0x003C0CC0,
     {0x8B, 0x91, 0x3C, 0x01, 0x00, 0x00, 0x80, 0x7A, 0x6C, 0x00}, 10},
    {0x003C0990,
     {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x25, 0xCD, 0x9C, 0x00}, 10},
    {0x003C0B00,
     {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x71, 0xCD, 0x9C, 0x00}, 10},
    0, 0, 0, 0, 0, 0,
};


constexpr FmodOperations kFmod = {
    &versions::v0132::InstallFmod,
};

constexpr InputOperations kInput = {
    &versions::v0132::input_backend::SetAppMain,
    &versions::v0132::input_backend::Handler,
    &versions::v0132::input_backend::InstallHooks,
    &versions::v0132::input_backend::InstallLifecycleHooks,
    &versions::v0132::input_backend::RequestSubmit,
    &versions::v0132::input_backend::PumpSubmit,
    &versions::v0132::input_backend::PostPointer,
    &versions::v0132::input_backend::PostKey,
    &versions::v0132::input_backend::PostChar,
    &versions::v0132::input_backend::ReadCursorState,
    &versions::v0132::input_backend::WriteCursorMode,
};

constexpr RuntimeOperations kRuntime = {
    &versions::v0132::Run, nullptr, nullptr,
};

constexpr VersionProfile kProfile = {
    Version::kV0_13_2,
    // TimeDateStamp not recorded yet: no stock 0.13.2 image was available when
    // the other two were measured. Detection falls back to SizeOfImage.
    0,
    0x008CC000,
    "0.13.2",
    L"MCPE0132Win32",
    SHIM_DISPLAY_NAME_W L" 0.13.2",
    &kEmptyLayout,
    &kLayout0132,
    &kFmod,
    &kInputPatches,
    &kInput,
    nullptr,
    nullptr,
    &kRuntime,
    VersionCapability::kRenderer0132Layout,
};

}  // namespace

const VersionProfile& Profile0132() noexcept {
  return kProfile;
}

}  // namespace shim::game
