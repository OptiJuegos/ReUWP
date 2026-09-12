#include "shim/game_profiles.h"

#include "shim/branding.h"
#include "shim/version_fmod.h"
#include "shim/version_runtime.h"
#include "shim/version_input.h"
#include "shim/version_patches.h"

namespace shim::game {
namespace {

// Minecraft 1.1.5 (SizeOfImage 0x01587000).
constexpr PatchLayout kPatchLayout = {
    16340584,
    16339296,
    16341816,
    16339132,
    16341544,
    {16341788, 16341632, 16341616, 16341628, 16341640},
    {16341532, 16341536},
    17156760,
    17156728,
    17156432,
};

constexpr InputLayout kInputLayout = {
    8060656,
    7166112,
    8060880,
    8050592,
    8060960,
    0,
    632,
    4,
    16,
    184,
    68,
    191,
    480,
};

constexpr RuntimeLayout kRuntimeLayout = {
    706240,
    7134944,
    7148064,
    0x006CDFC0,
};

constexpr D3DLayout kD3DLayout = {
    711824,
    21163440,
    7396352,
    172,
    188,
    268,
    256,
    276,
    284,
    288,
    0xC8,
    0x00E1F4F8,
    0x0070DB60,
    0x0142EDD8,
    0x0070DE60,
    0x0070D510,
};

constexpr FmodLayout kFmodLayout = {
    19140672,
    20705676,
    19140736,
    37,
    0,
    0,
    0,
};

constexpr Layout kLayout = {
    kPatchLayout,
    kInputLayout,
    kRuntimeLayout,
    kD3DLayout,
    kFmodLayout,
};


constexpr InputPatchCatalog kInputPatches = {
    {0x006DFEC0,
     {0x8B, 0x41, 0x04, 0x8B, 0x40, 0x04, 0x80, 0xB8, 0xB8, 0x00}, 10},
    {0x006DFE80,
     {0x8B, 0x41, 0x04, 0x8B, 0x40, 0x04, 0xC7, 0x40, 0x44, 0x02}, 10},
    {0x006DFE20,
     {0x8B, 0x41, 0x04, 0x8B, 0x50, 0x04, 0x80, 0xBA, 0xB8, 0x00}, 10},
    {0x006D4F20,
     {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x45, 0x0B, 0x2C, 0x01}, 10},
    {0x006D5340,
     {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xE1, 0x0B, 0x2C, 0x01}, 10},
    0, 0, 0, 0, 0, 0,
};

constexpr CodeSignatureSite kIdentitySignatures[] = {
    kInputPatches.mouse_relative,
    kInputPatches.mouse_release,
    kInputPatches.mouse_toggle,
    kInputPatches.text_show,
    kInputPatches.text_hide,
};

constexpr StdioPatchCatalog kStdioPatches = {
    0x00F958FC,
    0x00F958E0,
    0x00F95948,
    0x00F958F4,
    0x00F958F8,
};

constexpr StartupOperations kStartup = {
    nullptr,
    &versions::v115::InstallCompatibilityPatches,
    &versions::v115::InstallPreD3DPatches,
    &versions::v115::InstallPostWindowPatches,
};

constexpr FmodOperations kFmod = {
    &versions::v115::InstallFmod,
};

constexpr InputOperations kInput = {
    &versions::v115::input_backend::SetAppMain,
    &versions::v115::input_backend::Handler,
    &versions::v115::input_backend::InstallHooks,
    &versions::v115::input_backend::InstallLifecycleHooks,
    &versions::v115::input_backend::RequestSubmit,
    &versions::v115::input_backend::PumpSubmit,
    &versions::v115::input_backend::PostPointer,
    &versions::v115::input_backend::PostKey,
    &versions::v115::input_backend::PostChar,
    &versions::v115::input_backend::ReadCursorState,
    &versions::v115::input_backend::WriteCursorMode,
};

constexpr RuntimeOperations kRuntime = {
    nullptr, &versions::v115::BringUp, &versions::v115::RunFrame,
};

constexpr VersionProfile kProfile = {
    Version::kV1_1_5,
    0x5976D9CF,
    0x01587000,
    "1.1.5",
    L"MCPE115Win32",
    SHIM_DISPLAY_NAME_W L" 1.1.5",
    &kLayout,
    nullptr,
    &kFmod,
    &kInputPatches,
    &kInput,
    &kStdioPatches,
    &kStartup,
    &kRuntime,
    VersionCapability::kDrainCoreDispatcher |
        VersionCapability::kResourceActivation |
        VersionCapability::kStoragePickerActivation |
        VersionCapability::kWin7SpeechActivation |
        VersionCapability::kPreD3DCompatibility |
        VersionCapability::kStdioOverrides |
        VersionCapability::kRetainD3DDevicePair |
        VersionCapability::kAsyncInfoProjection |
        VersionCapability::kStorageV2Projection |
        VersionCapability::kAppMain115Diagnostics,
    kIdentitySignatures,
    CountOf(kIdentitySignatures),
};

}  // namespace

const VersionProfile& Profile115() noexcept {
  return kProfile;
}

}  // namespace shim::game
