#include "shim/version_fmod.h"

#include "shim/fmod_bridge.h"
#include "shim/fmod_install.h"
#include "shim/game_layout.h"

namespace shim::versions::v01510 {
namespace {

const void* const kBridgeSlots[] = {
    reinterpret_cast<const void*>(&fmod::bridge::System_Create),
    reinterpret_cast<const void*>(&fmod::bridge::getNumDrivers),
    reinterpret_cast<const void*>(&fmod::bridge::getDriverInfo),
    reinterpret_cast<const void*>(&fmod::bridge::setDriver),
    reinterpret_cast<const void*>(&fmod::bridge::setOutput),
    reinterpret_cast<const void*>(&fmod::bridge::getVersion),
    reinterpret_cast<const void*>(&fmod::bridge::init),
    reinterpret_cast<const void*>(&fmod::bridge::set3DSettings),
    reinterpret_cast<const void*>(&fmod::bridge::createChannelGroup),
    reinterpret_cast<const void*>(&fmod::bridge::getMasterChannelGroup),
    reinterpret_cast<const void*>(&fmod::bridge::addGroup),
    reinterpret_cast<const void*>(&fmod::bridge::Sound_release),
    reinterpret_cast<const void*>(&fmod::bridge::close),
    reinterpret_cast<const void*>(&fmod::bridge::System_release),
    reinterpret_cast<const void*>(&fmod::bridge::mixerResume),
    reinterpret_cast<const void*>(&fmod::bridge::mixerSuspend),
    reinterpret_cast<const void*>(&fmod::bridge::setMute),
    reinterpret_cast<const void*>(&fmod::bridge::setVolume),
    reinterpret_cast<const void*>(&fmod::bridge::createStream),
    reinterpret_cast<const void*>(&fmod::bridge::createSound),
    reinterpret_cast<const void*>(&fmod::bridge::set3DMinMaxDistance),
    reinterpret_cast<const void*>(&fmod::bridge::getNumSubSounds),
    reinterpret_cast<const void*>(&fmod::bridge::getSubSound),
    reinterpret_cast<const void*>(&fmod::bridge::playSound),
    reinterpret_cast<const void*>(&fmod::bridge::setPitch),
    reinterpret_cast<const void*>(&fmod::bridge::setPaused),
    reinterpret_cast<const void*>(&fmod::bridge::set3DAttributes),
    reinterpret_cast<const void*>(&fmod::bridge::isPlaying),
    reinterpret_cast<const void*>(&fmod::bridge::stop),
    reinterpret_cast<const void*>(&fmod::bridge::update),
    reinterpret_cast<const void*>(&fmod::bridge::set3DListenerAttributes),
};

}  // namespace

bool InstallFmod() noexcept {
  const game::FmodLayout* const layout = fmod::BoundLayout();
  if (layout == nullptr) {
    return false;
  }
  if (!fmod::InstallBridgeArray(
          layout->fmod_dll_name, layout->fmod_delay_iat,
          layout->fmod_bridge_slots, kBridgeSlots, CountOf(kBridgeSlots),
          "0.15.10")) {
    return false;
  }

  // Load and resolve the desktop runtime while startup still precedes HWND
  // creation. Failure remains non-fatal because the bridge supports silence.
  (void)fmod::PreloadBridgeRuntime();
  return true;
}

}  // namespace shim::versions::v01510
