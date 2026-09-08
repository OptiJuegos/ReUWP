#include "shim/version_fmod.h"

#include "shim/fmod_install.h"
#include "shim/game_layout.h"

namespace shim::versions::v115 {

bool InstallFmod() noexcept {
  const game::FmodLayout* const layout = fmod::BoundLayout();
  if (layout == nullptr) {
    return false;
  }
  return fmod::ResolveDesktopDelayImports(
      layout->fmod_delay_descriptor, layout->fmod_expected_iat,
      layout->fmod_expected_int, layout->fmod_import_count);
}

}  // namespace shim::versions::v115
