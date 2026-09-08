#include "fmod_internal.h"

#include <stdio.h>

#include "shim/log.h"

namespace shim::fmod::detail {
namespace {

struct LoaderState {
  Api api = {};
  HMODULE module = nullptr;
  bool load_attempted = false;
  bool loaded = false;
  OutputType output = kDefaultOutput;
  INIT_ONCE load_once = INIT_ONCE_STATIC_INIT;
};

LoaderState g_state = {};

const char* const kOutputNames[] = {
    "AUTODETECT", "OTHER", "OTHER", "OTHER", "OTHER",
    "OTHER",      "DSOUND", "WINMM", "WASAPI",
};

bool EqualsIgnoreCaseAscii(const char* text, const char* lowercase_literal,
                           size_t length) noexcept {
  for (size_t index = 0; index < length; ++index) {
    char character = text[index];
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character | 0x20);
    }
    if (character != lowercase_literal[index]) {
      return false;
    }
  }
  return true;
}

OutputType ReadRequestedOutput() noexcept {
  FILE* file = fopen(kOutputConfigFile, "rb");
  if (file == nullptr) {
    return kDefaultOutput;
  }

  char buffer[32] = {};
  const size_t read = fread(buffer, 1, sizeof(buffer) - 1, file);
  fclose(file);
  buffer[read] = '\0';

  if (EqualsIgnoreCaseAscii(buffer, "auto", 4)) {
    return OutputType::kAutodetect;
  }
  if (EqualsIgnoreCaseAscii(buffer, "dsound", 6)) {
    return OutputType::kDSound;
  }
  if (EqualsIgnoreCaseAscii(buffer, "winmm", 5)) {
    return OutputType::kWinMM;
  }
  return kDefaultOutput;
}

void* Resolve(const char* name) noexcept {
  return reinterpret_cast<void*>(::GetProcAddress(g_state.module, name));
}

bool ResolveRequired(void** slot, const char* name) noexcept {
  *slot = Resolve(name);
  if (*slot == nullptr) {
    log::Writef("FMOD bridge: missing export %s", name);
    return false;
  }
  return true;
}

#define RESOLVE_REQUIRED(field, name)                                    \
  if (!ResolveRequired(reinterpret_cast<void**>(&g_state.api.field), name)) {  \
    return false;                                                        \
  }

#define RESOLVE_OPTIONAL(field, name) \
  *reinterpret_cast<void**>(&g_state.api.field) = Resolve(name)

bool ResolveExports() noexcept {
  RESOLVE_REQUIRED(System_Create, "FMOD_System_Create");
  RESOLVE_REQUIRED(getVersion,
                   "?getVersion@System@FMOD@@QAG?AW4FMOD_RESULT@@PAI@Z");
  RESOLVE_REQUIRED(init, "?init@System@FMOD@@QAG?AW4FMOD_RESULT@@HIPAX@Z");
  RESOLVE_REQUIRED(set3DSettings,
                   "?set3DSettings@System@FMOD@@QAG?AW4FMOD_RESULT@@MMM@Z");
  RESOLVE_REQUIRED(
      createChannelGroup,
      "?createChannelGroup@System@FMOD@@QAG?AW4FMOD_RESULT@@PBDPAPAVChannelGroup@2@@Z");
  RESOLVE_REQUIRED(
      getMasterChannelGroup,
      "?getMasterChannelGroup@System@FMOD@@QAG?AW4FMOD_RESULT@@PAPAVChannelGroup@2@@Z");
  RESOLVE_REQUIRED(
      addGroup,
      "?addGroup@ChannelGroup@FMOD@@QAG?AW4FMOD_RESULT@@PAV12@_NPAPAVDSPConnection@2@@Z");
  RESOLVE_REQUIRED(Sound_release,
                   "?release@Sound@FMOD@@QAG?AW4FMOD_RESULT@@XZ");
  RESOLVE_REQUIRED(close, "?close@System@FMOD@@QAG?AW4FMOD_RESULT@@XZ");
  RESOLVE_REQUIRED(System_release,
                   "?release@System@FMOD@@QAG?AW4FMOD_RESULT@@XZ");
  RESOLVE_REQUIRED(mixerResume,
                   "?mixerResume@System@FMOD@@QAG?AW4FMOD_RESULT@@XZ");
  RESOLVE_REQUIRED(mixerSuspend,
                   "?mixerSuspend@System@FMOD@@QAG?AW4FMOD_RESULT@@XZ");
  RESOLVE_REQUIRED(setMute,
                   "?setMute@ChannelControl@FMOD@@QAG?AW4FMOD_RESULT@@_N@Z");
  RESOLVE_REQUIRED(setVolume,
                   "?setVolume@ChannelControl@FMOD@@QAG?AW4FMOD_RESULT@@M@Z");
  RESOLVE_REQUIRED(
      createStream,
      "?createStream@System@FMOD@@QAG?AW4FMOD_RESULT@@PBDIPAUFMOD_CREATESOUNDEXINFO@@PAPAVSound@2@@Z");
  RESOLVE_REQUIRED(
      createSound,
      "?createSound@System@FMOD@@QAG?AW4FMOD_RESULT@@PBDIPAUFMOD_CREATESOUNDEXINFO@@PAPAVSound@2@@Z");
  RESOLVE_REQUIRED(
      set3DMinMaxDistance,
      "?set3DMinMaxDistance@Sound@FMOD@@QAG?AW4FMOD_RESULT@@MM@Z");
  RESOLVE_REQUIRED(getNumSubSounds,
                   "?getNumSubSounds@Sound@FMOD@@QAG?AW4FMOD_RESULT@@PAH@Z");
  RESOLVE_REQUIRED(getSubSound,
                   "?getSubSound@Sound@FMOD@@QAG?AW4FMOD_RESULT@@HPAPAV12@@Z");
  RESOLVE_REQUIRED(
      playSound,
      "?playSound@System@FMOD@@QAG?AW4FMOD_RESULT@@PAVSound@2@PAVChannelGroup@2@_NPAPAVChannel@2@@Z");
  RESOLVE_REQUIRED(
      set3DAttributes,
      "?set3DAttributes@ChannelControl@FMOD@@QAG?AW4FMOD_RESULT@@PBUFMOD_VECTOR@@00@Z");
  RESOLVE_REQUIRED(setPitch,
                   "?setPitch@ChannelControl@FMOD@@QAG?AW4FMOD_RESULT@@M@Z");
  RESOLVE_REQUIRED(setPaused,
                   "?setPaused@ChannelControl@FMOD@@QAG?AW4FMOD_RESULT@@_N@Z");
  RESOLVE_REQUIRED(isPlaying,
                   "?isPlaying@ChannelControl@FMOD@@QAG?AW4FMOD_RESULT@@PA_N@Z");
  RESOLVE_REQUIRED(stop, "?stop@ChannelControl@FMOD@@QAG?AW4FMOD_RESULT@@XZ");
  RESOLVE_REQUIRED(update, "?update@System@FMOD@@QAG?AW4FMOD_RESULT@@XZ");
  RESOLVE_REQUIRED(
      set3DListenerAttributes,
      "?set3DListenerAttributes@System@FMOD@@QAG?AW4FMOD_RESULT@@HPBUFMOD_VECTOR@@000@Z");

  RESOLVE_OPTIONAL(
      setOutput,
      "?setOutput@System@FMOD@@QAG?AW4FMOD_RESULT@@W4FMOD_OUTPUTTYPE@@@Z");
  RESOLVE_OPTIONAL(
      getOutput,
      "?getOutput@System@FMOD@@QAG?AW4FMOD_RESULT@@PAW4FMOD_OUTPUTTYPE@@@Z");
  RESOLVE_OPTIONAL(setDriver, "?setDriver@System@FMOD@@QAG?AW4FMOD_RESULT@@H@Z");
  RESOLVE_OPTIONAL(ErrorString, "FMOD_ErrorString");
  RESOLVE_OPTIONAL(getNumDrivers,
                   "?getNumDrivers@System@FMOD@@QAG?AW4FMOD_RESULT@@PAH@Z");
  RESOLVE_OPTIONAL(
      getDriverInfo,
      "?getDriverInfo@System@FMOD@@QAG?AW4FMOD_RESULT@@HPADHPAUFMOD_GUID@@PAHPAW4FMOD_SPEAKERMODE@@2@Z");
  return true;
}

#undef RESOLVE_REQUIRED
#undef RESOLVE_OPTIONAL

bool LoadOnce() noexcept {
  g_state.output = ReadRequestedOutput();

  g_state.module = ::LoadLibraryW(L"fmod.dll");
  if (g_state.module == nullptr) {
    log::Writef("FMOD bridge: LoadLibraryW(fmod.dll) failed, error=%u",
                ::GetLastError());
    return false;
  }

  if (!ResolveExports()) {
    return false;
  }

  log::Writef("FMOD bridge: desktop DLL loaded, requested output=%d",
              static_cast<int>(g_state.output));
  return true;
}

BOOL CALLBACK LoadOnceCallback(PINIT_ONCE, PVOID, PVOID*) noexcept {
  g_state.loaded = LoadOnce();
  g_state.load_attempted = true;
  return TRUE;
}

}  // namespace

Api& ApiTable() noexcept {
  return g_state.api;
}

bool EnsureLoaded() noexcept {
  ::InitOnceExecuteOnce(&g_state.load_once, &LoadOnceCallback, nullptr, nullptr);
  return g_state.loaded;
}

OutputType RequestedOutput() noexcept {
  return g_state.output;
}

const char* OutputTypeName(int output) noexcept {
  if (output < 0 || static_cast<size_t>(output) >= CountOf(kOutputNames)) {
    return "OTHER";
  }
  return kOutputNames[output];
}

const char* ErrorText(Result result) noexcept {
  if (g_state.api.ErrorString == nullptr) {
    return "unknown";
  }
  const char* text = g_state.api.ErrorString(result);
  return text != nullptr ? text : "unknown";
}

}  // namespace shim::fmod::detail
