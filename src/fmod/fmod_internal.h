#pragma once

#include "shim/fmod_bridge.h"

namespace shim::fmod::detail {

#define FMOD_CALL __stdcall

struct Api {
  Result(FMOD_CALL* System_Create)(System**);
  Result(FMOD_CALL* getVersion)(System*, unsigned int*);
  Result(FMOD_CALL* init)(System*, int, unsigned int, void*);
  Result(FMOD_CALL* set3DSettings)(System*, float, float, float);
  Result(FMOD_CALL* createChannelGroup)(System*, const char*, ChannelGroup**);
  Result(FMOD_CALL* getMasterChannelGroup)(System*, ChannelGroup**);
  Result(FMOD_CALL* addGroup)(ChannelGroup*, ChannelGroup*, bool, void**);
  Result(FMOD_CALL* Sound_release)(Sound*);
  Result(FMOD_CALL* close)(System*);
  Result(FMOD_CALL* System_release)(System*);
  Result(FMOD_CALL* mixerResume)(System*);
  Result(FMOD_CALL* mixerSuspend)(System*);
  Result(FMOD_CALL* setMute)(void*, bool);
  Result(FMOD_CALL* setVolume)(void*, float);
  Result(FMOD_CALL* createStream)(System*, const char*, unsigned int, void*,
                                  Sound**);
  Result(FMOD_CALL* createSound)(System*, const char*, unsigned int, void*,
                                 Sound**);
  Result(FMOD_CALL* set3DMinMaxDistance)(Sound*, float, float);
  Result(FMOD_CALL* getNumSubSounds)(Sound*, int*);
  Result(FMOD_CALL* getSubSound)(Sound*, int, Sound**);
  Result(FMOD_CALL* playSound)(System*, Sound*, ChannelGroup*, bool, Channel**);
  Result(FMOD_CALL* set3DAttributes)(void*, const void*, const void*,
                                     const void*);
  Result(FMOD_CALL* setPitch)(void*, float);
  Result(FMOD_CALL* setPaused)(void*, bool);
  Result(FMOD_CALL* isPlaying)(void*, bool*);
  Result(FMOD_CALL* stop)(void*);
  Result(FMOD_CALL* update)(System*);
  Result(FMOD_CALL* set3DListenerAttributes)(System*, int, const void*,
                                             const void*, const void*,
                                             const void*);

  Result(FMOD_CALL* setOutput)(System*, int);
  Result(FMOD_CALL* getOutput)(System*, int*);
  Result(FMOD_CALL* setDriver)(System*, int);
  const char*(FMOD_CALL* ErrorString)(Result);
  Result(FMOD_CALL* getNumDrivers)(System*, int*);
  Result(FMOD_CALL* getDriverInfo)(System*, int, char*, int, void*, int*, int*,
                                   int*);
};

#undef FMOD_CALL

enum class OutputType : int {
  kAutodetect = 0,
  kDSound = 6,
  kWinMM = 7,
  kWasapi = 8,
};

inline constexpr OutputType kDefaultOutput = OutputType::kWasapi;
inline constexpr char kOutputConfigFile[] = "fmod_output.txt";

Api& ApiTable() noexcept;
bool EnsureLoaded() noexcept;
OutputType RequestedOutput() noexcept;
const char* OutputTypeName(int output) noexcept;
const char* ErrorText(Result result) noexcept;

System* SilentSystem() noexcept;
ChannelGroup* SilentGroup() noexcept;
Sound* SilentSound() noexcept;
Channel* SilentChannel() noexcept;
bool IsSilent(const void* object) noexcept;
bool ShouldUseSilent(const void* object) noexcept;

void SetMasterGroup(ChannelGroup* group) noexcept;
void SetSoundGroup(ChannelGroup* group) noexcept;
void SetMusicGroup(ChannelGroup* group) noexcept;
ChannelGroup* MasterGroup() noexcept;
ChannelGroup* SoundGroup() noexcept;
ChannelGroup* MusicGroup() noexcept;
bool AdvanceUpdateCounter() noexcept;
bool ShouldLogSoundCreate(Result result) noexcept;
bool ShouldLogPlaySound(Result result) noexcept;

bool StartsWithIgnoreCaseAscii(const char* text, const char* literal,
                               size_t length) noexcept;
const char* NormalizeAssetPath(const char* name_or_data, char* buffer,
                               size_t capacity) noexcept;
Result InitializeSystem(System* system, int max_channels, unsigned int flags,
                        void* extra_driver_data) noexcept;
Result CreateSoundWithNormalizedPath(System* system, const char* name_or_data,
                                     unsigned int mode, void* extra_info,
                                     Sound** sound) noexcept;
Result CreateStreamWithNormalizedPath(System* system, const char* name_or_data,
                                      unsigned int mode, void* extra_info,
                                      Sound** sound) noexcept;
void ForceAudible(ChannelGroup* group, const char* label) noexcept;
void ReassertForegroundAudio() noexcept;
Result ApplyMutePolicy(void* control, bool requested, bool* applied) noexcept;

}  // namespace shim::fmod::detail
