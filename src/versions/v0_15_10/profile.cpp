#include "shim/game_profiles.h"

#include "shim/branding.h"
#include "shim/version_fmod.h"
#include "shim/version_runtime.h"
#include "shim/version_input.h"
#include "shim/version_patches.h"

namespace shim::game {
namespace {

// Minecraft 0.15.10 (SizeOfImage 0x00D2E000).
constexpr PatchLayout kPatchLayout = {
    9774580,
    9773556,
    9775796,
    9773232,
    9775536,
    {9775744, 9775620, 9775736, 9775564, 9775548},
    {9775524, 9775528},
    10493980,
    10493004,
    0,
};

constexpr InputLayout kInputLayout = {
    3727280,
    6291088,
    0,
    0,
    3727984,
    3710512,
    600,
    0,
    16,
    136,
    68,
    0,
    0,
};

constexpr RuntimeLayout kRuntimeLayout = {
    6465648,
    6272912,
    0,
    0,
};

constexpr D3DLayout kD3DLayout = {
    6465952,
    11943496,
    7187968,
    152,
    164,
    256,
    244,
    264,
    272,
    276,
    0xAC,
    0x0087C113,
    0x006DAD60,
    0x00B63E4C,
    0x006DB060,
    0x006D4F40,
};

constexpr FmodLayout kFmodLayout = {
    0,
    0,
    0,
    0,
    10000928,
    12571116,
    31,
};

constexpr Layout kLayout = {
    kPatchLayout,
    kInputLayout,
    kRuntimeLayout,
    kD3DLayout,
    kFmodLayout,
};


constexpr InputPatchCatalog kInputPatches = {
    {0x005FFB10,
     {0x8B, 0x81, 0x58, 0x02, 0x00, 0x00, 0x80, 0xB8, 0x88, 0x00}, 10},
    {0x005FFB30,
     {0x8B, 0x81, 0x58, 0x02, 0x00, 0x00, 0xC7, 0x40, 0x44, 0x02}, 10},
    {0x005FFB40,
     {0x8B, 0x91, 0x58, 0x02, 0x00, 0x00, 0x80, 0xBA, 0x88, 0x00}, 10},
    {0x005FF810,
     {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x55, 0x53, 0xD0, 0x00}, 10},
    {0x005FF980,
     {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xA1, 0x53, 0xD0, 0x00}, 10},
    0x00B2C604,
    0x00B2C61C,
    0x00B2C620,
    0x000B8F10,
    0x000B8ED0,
    0x000B9FA0,
};

constexpr StartupOperations kStartup = {
    &versions::v01510::InstallCompatibilityPatches,
    nullptr,
    nullptr,
    &versions::v01510::InstallPostWindowPatches,
};

constexpr FmodOperations kFmod = {
    &versions::v01510::InstallFmod,
};

constexpr InputOperations kInput = {
    &versions::v01510::input_backend::SetAppMain,
    &versions::v01510::input_backend::Handler,
    &versions::v01510::input_backend::InstallHooks,
    &versions::v01510::input_backend::InstallLifecycleHooks,
    &versions::v01510::input_backend::RequestSubmit,
    &versions::v01510::input_backend::PumpSubmit,
    &versions::v01510::input_backend::PostPointer,
    &versions::v01510::input_backend::PostKey,
    &versions::v01510::input_backend::PostChar,
    &versions::v01510::input_backend::ReadCursorState,
    &versions::v01510::input_backend::WriteCursorMode,
};

constexpr RuntimeOperations kRuntime = {
    nullptr, &versions::v01510::BringUp, &versions::v01510::RunFrame,
};

constexpr VersionProfile kProfile = {
    Version::kV0_15_10,
    0x57EEFE46,
    0x00D2E000,
    "0.15.10",
    L"MCPE01510Win32",
    SHIM_DISPLAY_NAME_W L" 0.15.10",
    &kLayout,
    nullptr,
    &kFmod,
    &kInputPatches,
    &kInput,
    nullptr,
    &kStartup,
    &kRuntime,
    VersionCapability::kRetainD3DDevicePair,
};

}  // namespace

const VersionProfile& Profile01510() noexcept {
  return kProfile;
}

}  // namespace shim::game
