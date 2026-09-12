#include "shim/fmod_bridge.h"
#include "shim/runtime_trace.h"

#include "fmod_internal.h"
#include "shim/log.h"

namespace shim::fmod::bridge {

Result __stdcall System_Create(System** system) noexcept {
  if (!detail::EnsureLoaded()) {
    log::Write("FMOD shim: System_Create");
    if (system != nullptr) {
      *system = detail::SilentSystem();
    }
    return kOk;
  }

  const Result result = detail::ApiTable().System_Create(system);
  log::Writef("FMOD bridge: System_Create result=%d system=%p", result,
              system != nullptr ? *system : nullptr);

  // Un fallo aqui no se propaga: se entrega el centinela para que el juego siga
  // corriendo en silencio.
  if (system == nullptr || result != kOk || *system == nullptr) {
    log::Write("FMOD bridge: System_Create failed; using silent fallback");
    log::Write("FMOD shim: System_Create");
    if (system != nullptr) {
      *system = detail::SilentSystem();
    }
  }
  return kOk;
}

Result __stdcall init(System* system, int max_channels, unsigned int flags,
                      void* extra_driver_data) noexcept {
  if (detail::ShouldUseSilent(system)) {
    log::Write("FMOD shim: init");
    return kOk;
  }
  return detail::InitializeSystem(system, max_channels, flags, extra_driver_data);
}

Result __stdcall getVersion(System* system, unsigned int* version) noexcept {
  if (detail::ShouldUseSilent(system)) {
    log::Write("FMOD shim: getVersion");
    if (version != nullptr) {
      *version = static_cast<unsigned int>(-1);
    }
    return kOk;
  }
  const Result result = detail::ApiTable().getVersion(system, version);
  log::Writef("FMOD bridge: getVersion result=%d version=0x%x", result,
              version != nullptr ? *version : 0);
  return result;
}

Result __stdcall close(System* system) noexcept {
  return detail::ShouldUseSilent(system) ? kOk : detail::ApiTable().close(system);
}

Result __stdcall System_release(System* system) noexcept {
  return detail::ShouldUseSilent(system) ? kOk : detail::ApiTable().System_release(system);
}

Result __stdcall update(System* system) noexcept {
  if (detail::ShouldUseSilent(system)) {
    return kOk;
  }
  const Result result = detail::ApiTable().update(system);
  if (result == kOk) {
    if (detail::AdvanceUpdateCounter()) {
      detail::ReassertForegroundAudio();
    }
  } else {
    log::Writef("FMOD bridge: update -> %d (%s)", result, detail::ErrorText(result));
  }
  return result;
}

Result __stdcall mixerSuspend(System* system) noexcept {
  return detail::ShouldUseSilent(system) ? kOk : detail::ApiTable().mixerSuspend(system);
}

Result __stdcall mixerResume(System* system) noexcept {
  return detail::ShouldUseSilent(system) ? kOk : detail::ApiTable().mixerResume(system);
}

Result __stdcall getNumDrivers(System* system, int* count) noexcept {
  if (detail::ShouldUseSilent(system) || detail::ApiTable().getNumDrivers == nullptr) {
    if (count != nullptr) {
      *count = 0;
    }
    return kOk;
  }
  return detail::ApiTable().getNumDrivers(system, count);
}

Result __stdcall getDriverInfo(System* system, int index, char* name,
                               int name_length, void* guid, int* rate,
                               int* speaker_mode,
                               int* speaker_mode_channels) noexcept {
  if (detail::ShouldUseSilent(system) || detail::ApiTable().getDriverInfo == nullptr) {
    if (name != nullptr && name_length > 0) {
      name[0] = '\0';
    }
    if (guid != nullptr) {
      memset(guid, 0, 16);  // FMOD_GUID
    }
    if (rate != nullptr) {
      *rate = 0;
    }
    if (speaker_mode != nullptr) {
      *speaker_mode = 0;
    }
    if (speaker_mode_channels != nullptr) {
      *speaker_mode_channels = 0;
    }
    return kOk;
  }
  return detail::ApiTable().getDriverInfo(system, index, name, name_length, guid, rate,
                             speaker_mode, speaker_mode_channels);
}

Result __stdcall setDriver(System* system, int driver) noexcept {
  if (detail::ShouldUseSilent(system) || detail::ApiTable().setDriver == nullptr) {
    return kOk;
  }
  return detail::ApiTable().setDriver(system, driver);
}

Result __stdcall setOutput(System* system, int output) noexcept {
  if (detail::ShouldUseSilent(system) || detail::ApiTable().setOutput == nullptr) {
    return kOk;
  }
  return detail::ApiTable().setOutput(system, output);
}

Result __stdcall set3DSettings(System* system, float doppler_scale,
                               float distance_factor,
                               float rolloff_scale) noexcept {
  if (detail::ShouldUseSilent(system)) {
    log::Write("FMOD shim: set3DSettings");
    return kOk;
  }
  return detail::ApiTable().set3DSettings(system, doppler_scale, distance_factor,
                             rolloff_scale);
}

Result __stdcall set3DListenerAttributes(System* system, int listener,
                                         const void* position,
                                         const void* velocity,
                                         const void* forward,
                                         const void* up) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.set3DListenerAttributes.begin",
                        reinterpret_cast<uintptr_t>(system),
                        static_cast<uintptr_t>(listener));
  const Result result =
      detail::ShouldUseSilent(system)
          ? kOk
          : detail::ApiTable().set3DListenerAttributes(system, listener, position, velocity,
                                          forward, up);
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.set3DListenerAttributes.end",
                        reinterpret_cast<uintptr_t>(system),
                        reinterpret_cast<uintptr_t>(position), result);
  return result;
}

Result __stdcall createChannelGroup(System* system, const char* name,
                                    ChannelGroup** group) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.createChannelGroup.begin",
                        reinterpret_cast<uintptr_t>(system),
                        reinterpret_cast<uintptr_t>(name));
  if (detail::ShouldUseSilent(system)) {
    log::Write("FMOD shim: createChannelGroup");
    if (group != nullptr) {
      *group = detail::SilentGroup();
    }
    runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                          "FMOD.createChannelGroup.end",
                          reinterpret_cast<uintptr_t>(group != nullptr ? *group : nullptr),
                          0, kOk);
    return kOk;
  }

  const Result result = detail::ApiTable().createChannelGroup(system, name, group);
  if (result != kOk || group == nullptr || *group == nullptr) {
    if (result != kOk) {
      log::Writef("FMOD bridge: createChannelGroup -> %d (%s)", result,
                  detail::ErrorText(result));
    }
    runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                          "FMOD.createChannelGroup.end",
                          reinterpret_cast<uintptr_t>(group != nullptr ? *group : nullptr),
                          0, result);
    return result;
  }

  if (detail::StartsWithIgnoreCaseAscii(name, "music", 5)) {
    detail::SetMusicGroup(*group);
  } else if (detail::StartsWithIgnoreCaseAscii(name, "sound", 5)) {
    detail::SetSoundGroup(*group);
  }
  detail::ForceAudible(*group, name != nullptr ? name : "channel group");
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.createChannelGroup.end",
                        reinterpret_cast<uintptr_t>(*group), 0, result);
  return result;
}

Result __stdcall getMasterChannelGroup(System* system,
                                       ChannelGroup** group) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.getMasterChannelGroup.begin",
                        reinterpret_cast<uintptr_t>(system));
  if (detail::ShouldUseSilent(system)) {
    log::Write("FMOD shim: getMasterChannelGroup");
    if (group != nullptr) {
      *group = detail::SilentGroup();
    }
    runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                          "FMOD.getMasterChannelGroup.end",
                          reinterpret_cast<uintptr_t>(group != nullptr ? *group : nullptr),
                          0, kOk);
    return kOk;
  }

  const Result result = detail::ApiTable().getMasterChannelGroup(system, group);
  if (result == kOk && group != nullptr && *group != nullptr) {
    detail::SetMasterGroup(*group);
    detail::ForceAudible(*group, "master acquired");
  } else if (result != kOk) {
    log::Writef("FMOD bridge: getMasterChannelGroup -> %d (%s)", result,
                detail::ErrorText(result));
  }
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.getMasterChannelGroup.end",
                        reinterpret_cast<uintptr_t>(group != nullptr ? *group : nullptr),
                        0, result);
  return result;
}

Result __stdcall addGroup(ChannelGroup* group, ChannelGroup* child,
                          bool propagate_clock, void** connection) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.addGroup.begin",
                        reinterpret_cast<uintptr_t>(group),
                        reinterpret_cast<uintptr_t>(child));
  Result result = kOk;
  if (detail::ShouldUseSilent(group)) {
    if (connection != nullptr) {
      *connection = nullptr;
    }
  } else {
    result = detail::ApiTable().addGroup(group, child, propagate_clock, connection);
  }
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.addGroup.end",
                        reinterpret_cast<uintptr_t>(connection != nullptr ? *connection : nullptr),
                        propagate_clock ? 1u : 0u, result);
  return result;
}

Result __stdcall createSound(System* system, const char* name_or_data,
                             unsigned int mode, void* extra_info,
                             Sound** sound) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.createSound.begin",
                        reinterpret_cast<uintptr_t>(system), mode);
  if (detail::ShouldUseSilent(system)) {
    if (sound != nullptr) {
      *sound = detail::SilentSound();
    }
    runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                          "FMOD.createSound.end",
                          reinterpret_cast<uintptr_t>(sound != nullptr ? *sound : nullptr),
                          mode, kOk);
    return kOk;
  }
  const Result result =
      detail::CreateSoundWithNormalizedPath(system, name_or_data, mode, extra_info, sound);
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.createSound.end",
                        reinterpret_cast<uintptr_t>(sound != nullptr ? *sound : nullptr),
                        mode, result);
  return result;
}

Result __stdcall createStream(System* system, const char* name_or_data,
                              unsigned int mode, void* extra_info,
                              Sound** sound) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.createStream.begin",
                        reinterpret_cast<uintptr_t>(system), mode);
  if (detail::ShouldUseSilent(system)) {
    if (sound != nullptr) {
      *sound = detail::SilentSound();
    }
    runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                          "FMOD.createStream.end",
                          reinterpret_cast<uintptr_t>(sound != nullptr ? *sound : nullptr),
                          mode, kOk);
    return kOk;
  }
  const Result result =
      detail::CreateStreamWithNormalizedPath(system, name_or_data, mode, extra_info, sound);
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.createStream.end",
                        reinterpret_cast<uintptr_t>(sound != nullptr ? *sound : nullptr),
                        mode, result);
  return result;
}

Result __stdcall Sound_release(Sound* sound) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.Sound_release.begin",
                        reinterpret_cast<uintptr_t>(sound));
  const Result result = detail::ShouldUseSilent(sound) ? kOk : detail::ApiTable().Sound_release(sound);
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.Sound_release.end",
                        reinterpret_cast<uintptr_t>(sound), 0, result);
  return result;
}

Result __stdcall set3DMinMaxDistance(Sound* sound, float min,
                                     float max) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.set3DMinMaxDistance.begin",
                        reinterpret_cast<uintptr_t>(sound));
  const Result result =
      detail::ShouldUseSilent(sound) ? kOk : detail::ApiTable().set3DMinMaxDistance(sound, min, max);
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.set3DMinMaxDistance.end",
                        reinterpret_cast<uintptr_t>(sound), 0, result);
  return result;
}

Result __stdcall getNumSubSounds(Sound* sound, int* count) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.getNumSubSounds.begin",
                        reinterpret_cast<uintptr_t>(sound));
  Result result = kOk;
  if (detail::ShouldUseSilent(sound)) {
    if (count != nullptr) {
      *count = 0;
    }
  } else {
    result = detail::ApiTable().getNumSubSounds(sound, count);
  }
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.getNumSubSounds.end",
                        reinterpret_cast<uintptr_t>(sound),
                        count != nullptr ? static_cast<uintptr_t>(*count) : 0u,
                        result);
  return result;
}

Result __stdcall getSubSound(Sound* sound, int index, Sound** sub) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.getSubSound.begin",
                        reinterpret_cast<uintptr_t>(sound),
                        static_cast<uintptr_t>(index));
  Result result = kOk;
  if (detail::ShouldUseSilent(sound)) {
    if (sub != nullptr) {
      *sub = detail::SilentSound();
    }
  } else {
    result = detail::ApiTable().getSubSound(sound, index, sub);
  }
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.getSubSound.end",
                        reinterpret_cast<uintptr_t>(sub != nullptr ? *sub : nullptr),
                        static_cast<uintptr_t>(index), result);
  return result;
}

Result __stdcall playSound(System* system, Sound* sound, ChannelGroup* group,
                           bool paused, Channel** channel) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.playSound.begin",
                        reinterpret_cast<uintptr_t>(sound),
                        reinterpret_cast<uintptr_t>(group));
  if (detail::ShouldUseSilent(system) || detail::IsSilent(sound)) {
    if (channel != nullptr) {
      *channel = detail::SilentChannel();
    }
    runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                          "FMOD.playSound.end",
                          reinterpret_cast<uintptr_t>(channel != nullptr ? *channel : nullptr),
                          paused ? 1u : 0u, kOk);
    return kOk;
  }
  const Result result = detail::ApiTable().playSound(system, sound, group, paused, channel);
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.playSound.end",
                        reinterpret_cast<uintptr_t>(channel != nullptr ? *channel : nullptr),
                        paused ? 1u : 0u, result);
  if (detail::ShouldLogPlaySound(result)) {
    log::Writef("FMOD bridge: playSound result=%d paused=%d sound=%p group=%p "
                "channel=%p",
                result, paused, sound, group,
                channel != nullptr ? *channel : nullptr);
  }
  return result;
}

Result __stdcall setMute(void* control, bool mute) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.setMute.begin",
                        reinterpret_cast<uintptr_t>(control), mute ? 1u : 0u);
  if (detail::ShouldUseSilent(control)) {
    runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                          "FMOD.setMute.end",
                          reinterpret_cast<uintptr_t>(control), mute ? 1u : 0u,
                          kOk);
    return kOk;
  }

  bool applied = mute;
  const Result result = detail::ApplyMutePolicy(control, mute, &applied);
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.setMute.end",
                        reinterpret_cast<uintptr_t>(control), applied ? 1u : 0u,
                        result);
  log::Writef("FMOD bridge: setMute group=%p requested=%d applied=%d result=%d",
              control, mute ? 1 : 0, applied ? 1 : 0, result);
  return result;
}

Result __stdcall setVolume(void* control, float volume) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.setVolume.begin",
                        reinterpret_cast<uintptr_t>(control));
  const Result result = detail::ShouldUseSilent(control) ? kOk : detail::ApiTable().setVolume(control, volume);
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.setVolume.end",
                        reinterpret_cast<uintptr_t>(control), 0, result);
  return result;
}

Result __stdcall setPitch(void* control, float pitch) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.setPitch.begin",
                        reinterpret_cast<uintptr_t>(control));
  const Result result = detail::ShouldUseSilent(control) ? kOk : detail::ApiTable().setPitch(control, pitch);
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.setPitch.end",
                        reinterpret_cast<uintptr_t>(control), 0, result);
  return result;
}

Result __stdcall setPaused(void* control, bool paused) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.setPaused.begin",
                        reinterpret_cast<uintptr_t>(control), paused ? 1u : 0u);
  const Result result = detail::ShouldUseSilent(control) ? kOk : detail::ApiTable().setPaused(control, paused);
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.setPaused.end",
                        reinterpret_cast<uintptr_t>(control), paused ? 1u : 0u,
                        result);
  return result;
}

Result __stdcall isPlaying(void* control, bool* playing) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.isPlaying.begin",
                        reinterpret_cast<uintptr_t>(control));
  Result result = kOk;
  if (detail::ShouldUseSilent(control)) {
    if (playing != nullptr) {
      *playing = false;
    }
  } else {
    result = detail::ApiTable().isPlaying(control, playing);
  }
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.isPlaying.end",
                        reinterpret_cast<uintptr_t>(control),
                        playing != nullptr && *playing ? 1u : 0u, result);
  return result;
}

Result __stdcall stop(void* control) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.stop.begin",
                        reinterpret_cast<uintptr_t>(control));
  const Result result = detail::ShouldUseSilent(control) ? kOk : detail::ApiTable().stop(control);
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.stop.end",
                        reinterpret_cast<uintptr_t>(control), 0, result);
  return result;
}

Result __stdcall set3DAttributes(void* control, const void* position,
                                 const void* velocity,
                                 const void* alt_pan_pos) noexcept {
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.set3DAttributes.begin",
                        reinterpret_cast<uintptr_t>(control),
                        reinterpret_cast<uintptr_t>(position));
  if (detail::ShouldUseSilent(control)) {
    runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                          "FMOD.set3DAttributes.end",
                          reinterpret_cast<uintptr_t>(control), 0, kOk);
    return kOk;
  }
  const Result result =
      detail::ApiTable().set3DAttributes(control, position, velocity, alt_pan_pos);
  runtime_trace::Record(runtime_trace::BoundaryKind::Fmod,
                        "FMOD.set3DAttributes.end",
                        reinterpret_cast<uintptr_t>(control), 0, result);
  return result;
}

}  // namespace shim::fmod::bridge
