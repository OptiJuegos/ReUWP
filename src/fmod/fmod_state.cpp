#include "fmod_internal.h"

namespace shim::fmod::detail {
namespace {

struct FmodState {
  char silent_system_marker = 0;
  char silent_group_marker = 0;
  char silent_sound_marker = 0;
  char silent_channel_marker = 0;

  ChannelGroup* master_group = nullptr;
  ChannelGroup* sound_group = nullptr;
  ChannelGroup* music_group = nullptr;

  unsigned int update_counter = 0;
  unsigned int sound_create_log_count = 0;
  unsigned int play_sound_log_count = 0;
};

FmodState g_state = {};

}  // namespace

System* SilentSystem() noexcept {
  return &g_state.silent_system_marker;
}

ChannelGroup* SilentGroup() noexcept {
  return reinterpret_cast<ChannelGroup*>(&g_state.silent_group_marker);
}

Sound* SilentSound() noexcept {
  return reinterpret_cast<Sound*>(&g_state.silent_sound_marker);
}

Channel* SilentChannel() noexcept {
  return reinterpret_cast<Channel*>(&g_state.silent_channel_marker);
}

bool IsSilent(const void* object) noexcept {
  return object == &g_state.silent_system_marker ||
         object == &g_state.silent_group_marker ||
         object == &g_state.silent_sound_marker ||
         object == &g_state.silent_channel_marker;
}

bool ShouldUseSilent(const void* object) noexcept {
  return IsSilent(object) || !EnsureLoaded();
}

void SetMasterGroup(ChannelGroup* group) noexcept {
  g_state.master_group = group;
}

void SetSoundGroup(ChannelGroup* group) noexcept {
  g_state.sound_group = group;
}

void SetMusicGroup(ChannelGroup* group) noexcept {
  g_state.music_group = group;
}

ChannelGroup* MasterGroup() noexcept {
  return g_state.master_group;
}

ChannelGroup* SoundGroup() noexcept {
  return g_state.sound_group;
}

ChannelGroup* MusicGroup() noexcept {
  return g_state.music_group;
}

bool AdvanceUpdateCounter() noexcept {
  ++g_state.update_counter;
  return (g_state.update_counter % 120u) == 0u;
}

bool ShouldLogSoundCreate(Result result) noexcept {
  if (result != kOk || g_state.sound_create_log_count <= 0x2Fu) {
    ++g_state.sound_create_log_count;
    return true;
  }
  return false;
}

bool ShouldLogPlaySound(Result result) noexcept {
  if (result != kOk || g_state.play_sound_log_count <= 0x1Fu) {
    ++g_state.play_sound_log_count;
    return true;
  }
  return false;
}

}  // namespace shim::fmod::detail
