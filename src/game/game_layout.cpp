#include "shim/game_layout.h"

#include "shim/game_profile.h"
#include "shim/game_profiles.h"

namespace shim::game {
namespace {

constexpr Layout kNoLayout = {};
constexpr Version0132Layout kNoVersion0132Layout = {};

}  // namespace

const Layout& CurrentLayout() noexcept {
  const VersionProfile* const profile = CurrentProfile();
  if (profile == nullptr || profile->layout == nullptr) {
    return kNoLayout;
  }
  return *profile->layout;
}

const Version0132Layout& Layout0132() noexcept {
  const VersionProfile& profile = Profile0132();
  if (profile.layout0132 == nullptr) {
    return kNoVersion0132Layout;
  }
  return *profile.layout0132;
}

void* Resolve(DWORD rva) noexcept {
  const HMODULE base = Base();
  if (base == nullptr || rva == 0) {
    return nullptr;
  }
  return reinterpret_cast<unsigned char*>(base) + rva;
}

}  // namespace shim::game
